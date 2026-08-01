package com.zetla.ui.screens.chat

import android.net.Uri
import android.util.Log
import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.zetla.domain.model.ChatMessage
import com.zetla.domain.model.Conversation
import com.zetla.domain.model.FileAttachment
import com.zetla.domain.model.FileType
import com.zetla.domain.model.Model
import com.zetla.domain.model.ModelParams
import com.zetla.domain.model.Role
import com.zetla.domain.repository.ChatRepository
import com.zetla.data.ToolExecutorCallback
import com.zetla.data.ZetlaCore
import com.zetla.data.ZetlaPython
import com.zetla.domain.repository.ConfigRepository
import com.zetla.domain.repository.FileRepository
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.withTimeout
import java.util.UUID
import javax.inject.Inject

private const val TAG = "ZetlaNative"

@HiltViewModel
class ChatViewModel @Inject constructor(
    savedStateHandle: SavedStateHandle,
    private val chatRepository: ChatRepository,
    private val configRepository: ConfigRepository,
    private val fileRepository: FileRepository,
    @dagger.hilt.android.qualifiers.ApplicationContext private val appContext: android.content.Context
) : ViewModel() {

    private var sessionId: String = savedStateHandle["sessionId"] ?: ""
    private val _uiState = MutableStateFlow(DefaultChatUiState)

    val uiState: StateFlow<ChatUiState> = _uiState

    private var responseJob: Job? = null
    private var hasSentMessage = false

    init {
        viewModelScope.launch {
            chatRepository.refreshConversations()
            val apiKey = configRepository.getApiKey()
            val provider = configRepository.getProvider()
            val isConfigured = apiKey.isNotBlank() && provider.isNotBlank()
            val providers = configRepository.listProviders()
            val defaultParams = configRepository.getDefaultParams()
            _uiState.update {
                it.copy(
                    apiKey = apiKey,
                    isProviderConfigured = isConfigured,
                    showOnboarding = !isConfigured,
                    availableProviders = providers,
                    reasoningEffort = defaultParams.reasoningEffort
                )
            }
            if (isConfigured) {
                fetchModels()
            }
            // Reset stale sessionId from savedStateHandle if no matching conversation exists
            if (sessionId.isNotBlank()) {
                val convExists = chatRepository.getConversations().any {
                    it.id.toString() == sessionId
                }
                if (convExists && chatRepository.getHistory(sessionId).isNotEmpty()) {
                    loadHistory()
                    val conv = chatRepository.getConversations().find {
                        it.id.toString() == sessionId
                    }
                    if (conv != null) {
                        _uiState.update { it.copy(selectedConversation = conv) }
                    }
                } else {
                    sessionId = ""
                }
            }
            _uiState.update { it.copy(isInitialized = true) }
        }
        viewModelScope.launch {
            chatRepository.getConversationsFlow().collect { conversations ->
                _uiState.update { it.copy(conversations = conversations) }
            }
        }
        // Pre-cache models.dev data in background
        viewModelScope.launch(Dispatchers.IO) {
            configRepository.ensureModelsDevCached()
        }
    }

    private suspend fun createNewSession(): String {
        val selectedModel = _uiState.value.selectedModel
        var modelStr = selectedModel.id.ifEmpty { configRepository.getModel() }
        var providerForModel = selectedModel.provider.ifEmpty { configRepository.getProvider() }
        if (modelStr.isBlank()) {
            val allModels = _uiState.value.models
            if (allModels.isNotEmpty()) {
                val first = allModels.first()
                modelStr = first.id
                providerForModel = first.provider.ifEmpty { providerForModel }
            }
        }
        val currentProvider = configRepository.getProvider()
        if (providerForModel.isNotBlank() && providerForModel != currentProvider) {
            Log.d(TAG, "createNewSession: switching provider $currentProvider -> $providerForModel")
            configRepository.setProvider(providerForModel)
        }
        val userPrompt = configRepository.getSystemPrompt()
        val now = java.text.SimpleDateFormat("EEEE, MMMM d, yyyy", java.util.Locale.US).format(java.util.Date())
        val dynamicPrompt = buildDynamicPrompt(userPrompt, _uiState.value.isWebSearchEnabled, _uiState.value.isCodingEnabled, _uiState.value.isSpace, now)
        val modelForSession = if (providerForModel.isNotBlank()) Model(modelStr, modelStr, providerForModel) else Model(modelStr, modelStr)
        val conversation = chatRepository.createConversation(
            title = "New Chat",
            model = modelForSession,
            systemPrompt = dynamicPrompt
        )
        sessionId = conversation.id.toString()
        _uiState.update {
            it.copy(
                selectedConversation = conversation,
                selectedModel = modelForSession
            )
        }
        // Re-register tools for the new session (on IO dispatcher to avoid ANR)
        withContext(Dispatchers.IO) {
            if (_uiState.value.isCodingEnabled) {
                registerCodingTool()
            }
            if (_uiState.value.isWebSearchEnabled) {
                chatRepository.setSessionWebSearch(sessionId, true)
            }
        }
        Log.d(TAG, "Created new session: $sessionId model=$modelStr provider=$providerForModel")
        return modelStr
    }

    fun onUiEvent(event: ChatUiEvent) {
        when (event) {
            is ChatUiEvent.OnInputTextEdit -> _uiState.update { it.copy(inputText = event.str) }
            is ChatUiEvent.OnMessageSend -> viewModelScope.launch { sendMessage() }
            is ChatUiEvent.OnSetApiKey -> {
                configRepository.setApiKey(event.apiKey)
                _uiState.update { it.copy(apiKey = event.apiKey) }
            }
            is ChatUiEvent.OnFetchModels -> viewModelScope.launch { fetchModels() }
            is ChatUiEvent.OnModelSelected -> viewModelScope.launch { selectModel(event.model) }
            is ChatUiEvent.OnConversationSelected -> viewModelScope.launch {
                selectConversation(event.conversation)
            }
            is ChatUiEvent.OnNewChat -> newChat()
            is ChatUiEvent.OnNewSpace -> newSpace()
            is ChatUiEvent.OnRagToggled -> {} // no-op: always enabled in Space
            is ChatUiEvent.OnToggleSpaceFiles -> {
                viewModelScope.launch { refreshSpaceFiles() }
                _uiState.update { it.copy(showSpaceFiles = true) }
            }
            is ChatUiEvent.OnDismissSpaceFiles -> {
                _uiState.update { it.copy(showSpaceFiles = false) }
            }
            is ChatUiEvent.OnWebSearchTapped -> {
                val newEnabled = !_uiState.value.isWebSearchEnabled
                _uiState.update { it.copy(isWebSearchEnabled = newEnabled) }
                viewModelScope.launch {
                    chatRepository.setSessionWebSearch(sessionId, newEnabled)
                }
            }
            is ChatUiEvent.OnCodingToggled -> {
                val newEnabled = !_uiState.value.isCodingEnabled
                _uiState.update { it.copy(isCodingEnabled = newEnabled) }
                if (newEnabled) {
                    registerCodingTool()
                }
            }
            is ChatUiEvent.OnDeleteConversation -> viewModelScope.launch {
                deleteConversation(event.conversation)
            }
            is ChatUiEvent.OnUpdateConversation -> viewModelScope.launch {
                updateConversation(event.conversation)
            }
            is ChatUiEvent.OnConversationFilterSelected -> _uiState.update {
                it.copy(conversationFilter = event.filter)
            }
            is ChatUiEvent.OnStopRequest -> viewModelScope.launch { stopRequest() }
            is ChatUiEvent.OnPreloadMarkdownRequest -> {}
            is ChatUiEvent.OnCancelPreloadMarkdownJobs -> {}
            is ChatUiEvent.OnDismissOnboarding -> _uiState.update { it.copy(showOnboarding = false) }
            is ChatUiEvent.OnSetupProvider -> {
                viewModelScope.launch {
                    configRepository.setProviderConfig(event.providerId, event.apiKey, "", true)
                    configRepository.setProvider(event.providerId)
                    configRepository.setApiKey(event.apiKey)
                    fetchModels()
                    _uiState.update { it.copy(showOnboarding = false, isProviderConfigured = true) }
                }
            }
            is ChatUiEvent.OnRefreshConfig -> refreshConfig()
            is ChatUiEvent.OnAttachFile -> {
                if (_uiState.value.isSpace) {
                    // Space: index for RAG only, do NOT add to chat chips
                    viewModelScope.launch(Dispatchers.IO) {
                        indexFileForRag(event.file)
                        refreshSpaceFiles()
                    }
                } else {
                    _uiState.update { it.copy(attachedFiles = it.attachedFiles + event.file) }
                }
            }
            is ChatUiEvent.OnRemoveAttachedFile -> {
                val files = _uiState.value.attachedFiles.filter { it.id != event.fileId }
                _uiState.update { it.copy(attachedFiles = files) }
            }
            is ChatUiEvent.OnClearAttachedFiles -> {
                _uiState.update { it.copy(attachedFiles = emptyList()) }
            }
            is ChatUiEvent.OnReasoningEffortSelected -> {
                val effort = event.effort
                _uiState.update { it.copy(reasoningEffort = effort) }
                val current = configRepository.getDefaultParams()
                val updated = current.copy(reasoningEffort = effort)
                configRepository.setDefaultParams(updated)
                viewModelScope.launch {
                    if (sessionId.isNotBlank()) {
                        chatRepository.setSessionOptions(sessionId, updated.toOptionsJson())
                    }
                }
            }
            is ChatUiEvent.OnSpaceSetupDismiss -> {
                _uiState.update { it.copy(showSpaceSetup = false) }
            }
            is ChatUiEvent.OnSpaceSetupNameChanged -> {
                _uiState.update { it.copy(spaceSetupName = event.name) }
            }
            is ChatUiEvent.OnSpaceSetupFiles -> {
                val paths = event.files.map { it.path }
                val names = event.files.map { it.name }
                _uiState.update { it.copy(
                    spaceSetupFilePaths = it.spaceSetupFilePaths + paths,
                    spaceSetupFileNames = it.spaceSetupFileNames + names
                )}
            }
            is ChatUiEvent.OnSpaceSetupConfirm -> {
                Log.d(TAG, "SpaceSetup: confirm pressed, files=${_uiState.value.spaceSetupFileNames.size}")
                viewModelScope.launch(Dispatchers.Default) {
                    _uiState.update { it.copy(isIndexingSpace = true, spaceIndexProgress = "Creating space...") }
                    val name = _uiState.value.spaceSetupName.ifBlank { "New Space" }
                    Log.d(TAG, "SpaceSetup: creating session name='$name'")
                    createSpaceSession(name)
                    Log.d(TAG, "SpaceSetup: session created id=$sessionId")
                    
                    val paths = _uiState.value.spaceSetupFilePaths
                    val total = paths.size
                    var indexedCount = 0
                    for ((idx, path) in paths.withIndex()) {
                        try {
                            val f = java.io.File(path)
                            Log.d(TAG, "SpaceSetup: idx=${idx} path=$path exists=${f.exists()} len=${f.length()}")
                            _uiState.update { it.copy(spaceIndexProgress = "Reading ${f.name} (${idx+1}/$total)...") }
                            
                            val text: String?
                            if (f.name.endsWith(".pdf")) {
                                Log.d(TAG, "SpaceSetup: extracting PDF ${f.name}")
                                val result = fileRepository.extractPdfText(android.net.Uri.fromFile(f))
                                text = result.data
                                Log.d(TAG, "SpaceSetup: PDF done, text=${text?.length ?: 0} chars")
                            } else {
                                text = f.readText()
                            }
                            
                            if (!text.isNullOrBlank()) {
                                Log.d(TAG, "SpaceSetup: calling addSpaceFile for ${f.name}")
                                val result = chatRepository.addSpaceFile(sessionId.replace("-", ""), f.path, text)
                                Log.d(TAG, "SpaceSetup: addSpaceFile result=$result")
                                indexedCount++
                            }
                        } catch (e: Exception) {
                            Log.e(TAG, "SpaceSetup: error indexing ${path}", e)
                        }
                    }
                    Log.d(TAG, "SpaceSetup: indexed $indexedCount/$total files, refreshing...")
                    refreshSpaceFiles()
                    // Save RAG index to disk (Lorentz vectors + chunk texts only, <1MB)
                    val nativeSid = sessionId.replace("-", "")
                    val ragDir = java.io.File(appContext.filesDir, "rag_$nativeSid")
                    ragDir.mkdirs()
                    val saved = ZetlaCore.nativeSaveRagSession(nativeSid, ragDir.absolutePath)
                    Log.d(TAG, "SpaceSetup: RAG save to disk=$saved dir=${ragDir.absolutePath}")
                    _uiState.update { it.copy(showSpaceSetup = false, isIndexingSpace = false, spaceIndexProgress = "") }
                    Log.d(TAG, "SpaceSetup: complete")
                }
            }
        }
    }

    private fun buildDynamicPrompt(userPrompt: String, isWebSearch: Boolean, isCoding: Boolean, isSpace: Boolean, dateStr: String): String {
        val toolParts = mutableListOf<String>()
        
        if (isSpace) {
            toolParts.add("""
You are in a SPACE: a knowledge-augmented chat session. You have access to the search_files tool which searches through all uploaded documents in this space.

Tool: search_files
  - query: your search query (keep it specific and concise - 3-8 words works best)
  - top_k: number of results (default 10)
  
Before invoking search_files, use the rewrite_query tool to optimize your query for better retrieval:
  1. Think about what specific terms, phrases, or entity names would appear in the documents
  2. Expand abbreviations, add synonyms, use domain-specific terminology
  3. The rewrite_query tool takes your original query and returns a rewritten version

When to use search_files:
  - User asks about content that may be in the uploaded documents
  - User asks "what does the document say about X"
  - User needs specific facts, figures, or passages from files
  - Any factual query that could benefit from document retrieval

When NOT to use it:
  - General conversation, greetings, simple math
  - Questions clearly unrelated to uploaded content
  - User explicitly asks you NOT to search

Search results return [file_path, chunk_text, similarity_score]. Cite the source file when using retrieved content.

You also have the list_corpus_files tool to see which documents are available in the corpus.
Use it to understand what files exist before formulating search queries.
""".trimIndent())
        }
        if (isWebSearch) {
            toolParts.add("""
You have access to the web_search tool with two modes:
  1. mode='search' + query -> get search results (titles, URLs, snippets)
  2. mode='fetch' + url -> get the full content of a web page

When to use each mode:
• "who won the latest match" -> use search mode
• "what's the weather in Paris" -> use search mode
• "I need the full article from this link https://..." -> use fetch mode to read the entire page
• "summarize this page: https://..." -> use fetch mode
• "latest news about AI" -> use search, then fetch promising URLs for deeper info

The search tool returns titles, URLs and snippets. Use fetch mode with specific URLs to get complete page content.
""".trimIndent())
        }
        if (isCoding) {
            toolParts.add("""
You have access to the run_code tool (Python, standard library only). Use it for computation, data processing, and verification.

When to use run_code:
• "calculate 15% tip on $47.50" -> compute the exact amount
• "what's 2^10" -> compute powers
• "convert 100 USD to EUR at rate 0.92" -> do the conversion
• "sort these numbers: 42, 7, 19, 3" -> run a quick sort
• "is 97 prime?" -> check primality

Available modules: math, decimal, fractions, random, json, re, collections, itertools, functools.
No pip, no internet, no filesystem access.
""".trimIndent())
        }
        return buildString {
            if (userPrompt.isNotBlank()) {
                append(userPrompt)
                append("\n\n")
            }
            if (toolParts.isNotEmpty()) {
                append(toolParts.joinToString("\n"))
                append("\n\n")
            }
            append("Current date: $dateStr.")
        }
    }

    private fun refreshConfig() {
        val apiKey = configRepository.getApiKey()
        val provider = configRepository.getProvider()
        val isConfigured = apiKey.isNotBlank() && provider.isNotBlank()
        val providers = configRepository.listProviders()
        _uiState.update {
            it.copy(
                apiKey = apiKey,
                isProviderConfigured = isConfigured,
                showOnboarding = !isConfigured && _uiState.value.showOnboarding,
                availableProviders = providers
            )
        }
        if (isConfigured) {
            viewModelScope.launch { fetchModels() }
        }
    }

    private fun registerCodingTool() {
        val nativeSid = sessionId.replace("-", "")
        val schema = """
        {"type":"object","properties":{"code":{"type":"string","description":"Python code using ONLY standard library modules (math, decimal, fractions, random, json, re, collections, itertools, functools). No pip, no subprocess, no internet."}},"required":["code"]}
        """.trimIndent()
        val description = "Execute Python code. ONLY standard library modules are available (math, decimal, fractions, random, json, re, collections, itertools, functools). " +
            "No pip packages, no internet access, no filesystem access. " +
            "Do NOT use os, sys, pathlib, subprocess, or any module for filesystem, networking, or process spawning. " +
            "Use this for mathematical computations, data processing, and algorithmic tasks."

        ZetlaCore.nativeAddTool(nativeSid, "run_code", description, schema)

        val executor = object : ToolExecutorCallback {
            override fun execute(sessionId: String, toolName: String, argumentsJson: String): String {
                try {
                    val obj = org.json.JSONObject(argumentsJson)
                    val code = obj.optString("code", "")
                    if (code.isBlank()) return """{"error":"Missing code argument"}"""
                    val result = ZetlaPython.execute(code, timeoutMs = 30_000)
                    val output = buildString {
                        if (result.output.isNotBlank()) append(result.output)
                        if (result.error != null) {
                            if (isNotBlank()) append("\n")
                            append("STDERR: ${result.error}")
                        }
                    }
                    return """{"output":${org.json.JSONObject.quote(output)}}"""
                } catch (e: Exception) {
                    return """{"error":"${e.message?.replace("\"", "\\\"") ?: "Unknown error"}"}"""
                }
            }
        }
        ZetlaCore.nativeSetToolExecutor(nativeSid, executor)
    }

    private suspend fun stopRequest() {
        chatRepository.cancelRequest()
        responseJob?.cancelAndJoin()
        responseJob = null
        _uiState.update {
            it.copy(
                isLoadingResponse = false,
                isStreamingResponse = false,
                streamingResponse = null,
                streamingThinking = null
            )
        }
    }

    private suspend fun updateConversation(conversation: Conversation) {
        Log.d(TAG, "updateConversation: ${conversation.id} title='${conversation.title}' starred=${conversation.isStarred}")
        chatRepository.updateConversation(conversation)
        _uiState.update { it.copy(selectedConversation = conversation) }
    }

    private suspend fun sendMessage() {
        val state = _uiState.value
        var text = state.inputText
        if (text.isEmpty() || state.isLoadingResponse || state.isStreamingResponse) return

        if (sessionId.isBlank()) {
            createNewSession()
        }

        val attachedFiles = state.attachedFiles
        val hasFiles = attachedFiles.isNotEmpty()

        val currentMessages = state.messages.toMutableList().apply {
            add(UiMessage(id = UUID.randomUUID().toString(), content = text, isUser = true))
        }

        _uiState.update {
            it.copy(
                messages = currentMessages,
                inputText = "",
                isLoadingResponse = true
            )
        }

        val apiKey = configRepository.getApiKey()
        if (apiKey.isNullOrBlank()) {
            val errorMsg = UiMessage(
                id = UUID.randomUUID().toString(),
                content = "API key is not set.",
                isUser = false
            )
            _uiState.update {
                it.copy(
                    messages = it.messages + errorMsg,
                    isLoadingResponse = false
                )
            }
            return
        }

        // Auto-title: set title from first user message (skip for spaces - name set during creation)
        if (!hasSentMessage && !_uiState.value.isSpace) {
            hasSentMessage = true
            val conv = _uiState.value.selectedConversation
            if (conv != null) {
                val updated = conv.copy(title = text.take(60))
                updateConversation(updated)
            }
        }

        val response = StringBuilder()
        val thinkingText = StringBuilder()
        responseJob = viewModelScope.launch(Dispatchers.IO) {
            // Check and run compaction if needed
            if (sessionId.isNotBlank() && chatRepository.needsCompaction(sessionId)) {
                _uiState.update {
                    it.copy(streamingThinking = "Compacting Conversation...")
                }
                chatRepository.compactSession(sessionId)
            }
            _uiState.update { it.copy(streamingThinking = null) }

            // --- PDF extraction on Kotlin side ---
            val pdfFiles = attachedFiles.filter { it.type == FileType.PDF }
            val nonPdfFiles = attachedFiles.filter { it.type != FileType.PDF }
            var hasImageUris = false
            val imageUris = mutableListOf<String>()
            var finalText = text

            if (pdfFiles.isNotEmpty()) {
                val supportsVision = _uiState.value.selectedModel.capabilities.supportsVision
                _uiState.update { it.copy(isExtractingPdf = true, streamingThinking = "Extracting PDF content...") }

                val pdfContent = StringBuilder()
                for (pdfFile in pdfFiles) {
                    try {
                        val uri = Uri.fromFile(java.io.File(pdfFile.path))
                        if (supportsVision) {
                            val result = fileRepository.extractPdfWithImages(uri)
                            if (result.success) {
                                val data = result.data!!
                                pdfContent.appendLine("--- Content from ${pdfFile.name} ---")
                                pdfContent.appendLine(data.text)
                                pdfContent.appendLine("---")
                                imageUris.addAll(data.imageDataUris)
                                if (data.imageDataUris.isNotEmpty()) hasImageUris = true
                            } else {
                                Log.w(TAG, "PDF extraction failed for ${pdfFile.name}: ${result.error}")
                                pdfContent.appendLine("[Failed to extract ${pdfFile.name}: ${result.error}]")
                            }
                        } else {
                            val result = fileRepository.extractPdfText(uri)
                            if (result.success) {
                                pdfContent.appendLine("--- Content from ${pdfFile.name} ---")
                                pdfContent.appendLine(result.data!!)
                                pdfContent.appendLine("---")
                            } else {
                                Log.w(TAG, "PDF extraction failed for ${pdfFile.name}: ${result.error}")
                                pdfContent.appendLine("[Failed to extract ${pdfFile.name}: ${result.error}]")
                            }
                        }
                    } catch (e: Exception) {
                        Log.e(TAG, "PDF extraction exception for ${pdfFile.name}", e)
                        pdfContent.appendLine("[Error extracting ${pdfFile.name}: ${e.message}]")
                    }
                }

                _uiState.update { it.copy(isExtractingPdf = false, streamingThinking = null) }

                if (pdfContent.isNotEmpty()) {
                    finalText = "${pdfContent.toString().trimEnd()}\n\n---\n$text"
                }
            }

            // --- RAG search for Space sessions (inject as system context, not user text) ---
            var ragContext: String? = null
            if (_uiState.value.isSpace && sessionId.isNotBlank()) {
                val nativeSid = sessionId.replace("-", "")
                Log.d(TAG, "RAG search: query=${finalText.take(80)}...")
                try {
                    val ragJson = ZetlaCore.nativeRagSearch(nativeSid, finalText, 10, "")
                    val ragResults = org.json.JSONArray(ragJson)
                    if (ragResults.length() > 0) {
                        val sb = StringBuilder()
                        sb.appendLine("[RAG Search Results]")
                        for (i in 0 until ragResults.length()) {
                            val item = ragResults.getJSONObject(i)
                            val path = item.optString("path", "unknown")
                            val fileName = java.io.File(path).name.ifEmpty { path }
                            val score = item.optDouble("score", 0.0)
                            val chunkText = item.optString("text", item.optString("snippet", ""))
                            if (chunkText.isNotBlank()) {
                                sb.appendLine("--- ${fileName} (score: ${"%.2f".format(score)}) ---")
                                sb.appendLine(chunkText.take(800))
                                sb.appendLine()
                            }
                        }
                        ragContext = sb.toString().trimEnd()
                    }
                } catch (e: Exception) {
                    Log.w(TAG, "RAG search failed: ${e.message}")
                }
            }

            try {
                withTimeout(90_000L) {
                    val modelId = _uiState.value.selectedModel.id.ifEmpty { configRepository.getModel() }
                    Log.d(TAG, "sendMessage: session=$sessionId model=$modelId text='$finalText' pdfFiles=${pdfFiles.size} nonPdfFiles=${nonPdfFiles.size} imageUris=${imageUris.size}")

                    val params = configRepository.getDefaultParams()
                    chatRepository.setSessionOptions(sessionId, params.toOptionsJson())

                    val state = _uiState.value
                    val userPrompt = configRepository.getSystemPrompt()
                    val now = java.text.SimpleDateFormat("EEEE, MMMM d, yyyy", java.util.Locale.US).format(java.util.Date())
                    val dynamicPrompt = buildDynamicPrompt(userPrompt, state.isWebSearchEnabled, state.isCodingEnabled, state.isSpace, now)
                    
                    // Inject RAG context into system prompt if available
                    val finalPrompt = if (ragContext != null) "$dynamicPrompt\n\n$ragContext" else dynamicPrompt
                    chatRepository.setSessionSystemPrompt(sessionId, finalPrompt)

                    val success: Boolean
                    when {
                        hasImageUris && imageUris.isNotEmpty() -> {
                            success = chatRepository.sendMessageFullWithImages(
                                sessionId = sessionId,
                                message = finalText,
                                model = modelId,
                                imageDataUris = imageUris.toTypedArray(),
                                onToken = { content, reasoning ->
                                    if (reasoning != null) thinkingText.append(reasoning)
                                    if (content != null) response.append(content)
                                    _uiState.update {
                                        it.copy(
                                            streamingResponse = response.toString(),
                                            streamingThinking = thinkingText.toString().ifEmpty { null },
                                            isLoadingResponse = false,
                                            isStreamingResponse = true
                                        )
                                    }
                                },
                                onFinished = {
                                    val finalResp = response.toString()
                                    if (finalResp.isBlank() || finalResp.startsWith("Error:")) {
                                        _uiState.update {
                                            it.copy(
                                                streamingResponse = null,
                                                streamingThinking = null,
                                                isLoadingResponse = false,
                                                isStreamingResponse = false,
                                                attachedFiles = emptyList()
                                            )
                                        }
                                    } else {
                                        Log.d(TAG, "sendMessage finished: session=$sessionId")
                                        _uiState.update {
                                            val finalMessages = it.messages + UiMessage(
                                                id = UUID.randomUUID().toString(),
                                                content = finalResp,
                                                thinkingText = thinkingText.toString().ifEmpty { null },
                                                isUser = false
                                            )
                                            it.copy(
                                                messages = finalMessages,
                                                streamingResponse = null,
                                                streamingThinking = null,
                                                isLoadingResponse = false,
                                                isStreamingResponse = false,
                                                attachedFiles = emptyList()
                                            )
                                        }
                                    }
                                }
                            )
                        }
                        nonPdfFiles.isNotEmpty() -> {
                            val fileIds = nonPdfFiles.mapNotNull { file ->
                                fileRepository.addFile(sessionId, file.path).data?.id
                            }
                            success = chatRepository.sendMessageFullWithFiles(
                                sessionId = sessionId,
                                message = finalText,
                                model = modelId,
                                fileIds = fileIds.toTypedArray(),
                                onToken = { content, reasoning ->
                                    if (reasoning != null) thinkingText.append(reasoning)
                                    if (content != null) response.append(content)
                                    _uiState.update {
                                        it.copy(
                                            streamingResponse = response.toString(),
                                            streamingThinking = thinkingText.toString().ifEmpty { null },
                                            isLoadingResponse = false,
                                            isStreamingResponse = true
                                        )
                                    }
                                },
                                onFinished = {
                                    val finalResp = response.toString()
                                    if (finalResp.isBlank() || finalResp.startsWith("Error:")) {
                                        _uiState.update {
                                            it.copy(
                                                streamingResponse = null,
                                                streamingThinking = null,
                                                isLoadingResponse = false,
                                                isStreamingResponse = false,
                                                attachedFiles = emptyList()
                                            )
                                        }
                                    } else {
                                        Log.d(TAG, "sendMessage finished: session=$sessionId")
                                        _uiState.update {
                                            val finalMessages = it.messages + UiMessage(
                                                id = UUID.randomUUID().toString(),
                                                content = finalResp,
                                                thinkingText = thinkingText.toString().ifEmpty { null },
                                                isUser = false
                                            )
                                            it.copy(
                                                messages = finalMessages,
                                                streamingResponse = null,
                                                streamingThinking = null,
                                                isLoadingResponse = false,
                                                isStreamingResponse = false,
                                                attachedFiles = emptyList()
                                            )
                                        }
                                    }
                                }
                            )
                        }
                        else -> {
                            success = chatRepository.sendMessageFull(
                                sessionId = sessionId,
                                message = finalText,
                                model = modelId,
                                onToken = { content, reasoning ->
                                    if (reasoning != null) thinkingText.append(reasoning)
                                    if (content != null) response.append(content)
                                    _uiState.update {
                                        it.copy(
                                            streamingResponse = response.toString(),
                                            streamingThinking = thinkingText.toString().ifEmpty { null },
                                            isLoadingResponse = false,
                                            isStreamingResponse = true
                                        )
                                    }
                                },
                                onFinished = {
                                    val finalResp = response.toString()
                                    if (finalResp.isBlank() || finalResp.startsWith("Error:")) {
                                        _uiState.update {
                                            it.copy(
                                                streamingResponse = null,
                                                streamingThinking = null,
                                                isLoadingResponse = false,
                                                isStreamingResponse = false,
                                                attachedFiles = emptyList()
                                            )
                                        }
                                    } else {
                                        Log.d(TAG, "sendMessage finished: session=$sessionId")
                                        _uiState.update {
                                            val finalMessages = it.messages + UiMessage(
                                                id = UUID.randomUUID().toString(),
                                                content = finalResp,
                                                thinkingText = thinkingText.toString().ifEmpty { null },
                                                isUser = false
                                            )
                                            it.copy(
                                                messages = finalMessages,
                                                streamingResponse = null,
                                                streamingThinking = null,
                                                isLoadingResponse = false,
                                                isStreamingResponse = false,
                                                attachedFiles = emptyList()
                                            )
                                        }
                                    }
                                }
                            )
                        }
                    }
                    if (!success) {
                        Log.e(TAG, "sendMessage failed: session=$sessionId")
                        _uiState.update {
                            val errorMessages = it.messages + UiMessage(
                                id = UUID.randomUUID().toString(),
                                content = "Request failed. Check your API key and connection.",
                                isUser = false
                            )
                            it.copy(
                                messages = errorMessages,
                                streamingResponse = null,
                                streamingThinking = null,
                                isLoadingResponse = false,
                                isStreamingResponse = false
                            )
                        }
                    }
                }
            } catch (_: TimeoutCancellationException) {
                Log.e(TAG, "sendMessage timed out: session=$sessionId")
                _uiState.update {
                    it.copy(
                        isExtractingPdf = false,
                        isLoadingResponse = false,
                        isStreamingResponse = false,
                        streamingResponse = null,
                        streamingThinking = null,
                        error = "Request timed out. Please try again."
                    )
                }
            } catch (e: Exception) {
                Log.e(TAG, "sendMessage exception", e)
                _uiState.update {
                    it.copy(
                        isExtractingPdf = false,
                        isLoadingResponse = false,
                        isStreamingResponse = false,
                        streamingResponse = null,
                        streamingThinking = null,
                        error = "Request failed: ${e.message}"
                    )
                }
            }
        }
    }

    private suspend fun fetchModels() {
        _uiState.update { it.copy(isFetchingModels = true) }
        val allModels = configRepository.fetchAllProviderModels()
        if (allModels.isEmpty()) {
            val models = chatRepository.fetchModels()
            _uiState.update { state ->
                val currentId = state.selectedModel.id
                val preserved = models.find { m -> m.id == currentId }
                state.copy(
                    models = models,
                    selectedModel = preserved ?: if (models.isNotEmpty()) models.first() else Model.defaultModel,
                    isFetchingModels = false,
                    modelsByProvider = models.groupBy { it.provider.ifEmpty { "default" } }
                )
            }
        } else {
            val visionModels = allModels.filter { it.capabilities.supportsVision }
            if (visionModels.isNotEmpty()) {
                Log.d(TAG, "fetchModels: vision-capable models: ${visionModels.map { it.id }}")
            }
            val thinkingModels = allModels.filter { it.capabilities.thinkingLevels.isNotEmpty() }
            if (thinkingModels.isNotEmpty()) {
                Log.d(TAG, "fetchModels: models with thinking levels: ${thinkingModels.map { "${it.id}=${it.capabilities.thinkingLevels}" }}")
            }
            _uiState.update { state ->
                val currentId = state.selectedModel.id
                val preserved = allModels.find { m -> m.id == currentId && m.provider == state.selectedModel.provider }
                val newModel = preserved ?: if (allModels.isNotEmpty()) allModels.first() else Model.defaultModel
                val newLevels = newModel.capabilities.thinkingLevels
                Log.d(TAG, "fetchModels: selected model='${newModel.id}' supportsVision=${newModel.capabilities.supportsVision} thinkingLevels=${newLevels} reasoningEffort=${_uiState.value.reasoningEffort}")
                state.copy(
                    models = allModels,
                    selectedModel = newModel,
                    isFetchingModels = false,
                    modelsByProvider = allModels.groupBy { it.provider.ifEmpty { "default" } }
                )
            }
            // Ensure the active provider matches the selected model
            val selected = _uiState.value.selectedModel
            if (selected.provider.isNotEmpty()) {
                val currentProvider = configRepository.getProvider()
                if (selected.provider != currentProvider) {
                    Log.d(TAG, "fetchModels: switching provider $currentProvider -> ${selected.provider}")
                    configRepository.setProvider(selected.provider)
                }
            }
        }
    }

    private suspend fun selectModel(model: Model) {
        val currentProvider = configRepository.getProvider()
        val newProvider = model.provider.ifEmpty { currentProvider }
        if (newProvider != currentProvider) {
            Log.d(TAG, "Switching provider: $currentProvider -> $newProvider for model ${model.id}")
            configRepository.setProvider(newProvider)
        }
        configRepository.setModel(model.id)
        _uiState.update { it.copy(selectedModel = model) }
    }

    private fun newChat() {
        hasSentMessage = false
        viewModelScope.launch {
            createNewSession()
            _uiState.update {
                it.copy(
                    messages = emptyList(),
                    isSpace = false,
                    isRagEnabled = false,
                    spaceFileNames = emptyList()
                )
            }
        }
    }

    private fun newSpace() {
        Log.d(TAG, "newSpace: opening setup modal")
        hasSentMessage = false
        _uiState.update { it.copy(showSpaceSetup = true, spaceSetupName = "", spaceSetupFilePaths = emptyList(), spaceSetupFileNames = emptyList()) }
    }
    
    private suspend fun createSpaceSession(name: String) {
        Log.d(TAG, "createSpaceSession: name='$name'")
        val selectedModel = _uiState.value.selectedModel
        var modelStr = selectedModel.id.ifEmpty { configRepository.getModel() }
        var providerForModel = selectedModel.provider.ifEmpty { configRepository.getProvider() }
        if (modelStr.isBlank()) {
            val allModels = _uiState.value.models
            if (allModels.isNotEmpty()) {
                val first = allModels.first()
                modelStr = first.id
                providerForModel = first.provider.ifEmpty { providerForModel }
            }
        }
        val currentProvider = configRepository.getProvider()
        if (providerForModel.isNotBlank() && providerForModel != currentProvider) {
            configRepository.setProvider(providerForModel)
        }
        val userPrompt = configRepository.getSystemPrompt()
        val now = java.text.SimpleDateFormat("EEEE, MMMM d, yyyy", java.util.Locale.US).format(java.util.Date())
        val dynamicPrompt = buildDynamicPrompt(userPrompt, false, false, true, now)
        val modelForSession = if (providerForModel.isNotBlank()) Model(modelStr, modelStr, providerForModel) else Model(modelStr, modelStr)
        val conversation = chatRepository.createSpace(
            title = name.ifBlank { "New Space" },
            model = modelForSession,
            systemPrompt = dynamicPrompt
        )
        sessionId = conversation.id.toString()
        val nativeSid = sessionId.replace("-", "")
        Log.d(TAG, "createSpaceSession: sessionId=$sessionId nativeId=$nativeSid")
        _uiState.update {
            it.copy(
                selectedConversation = conversation,
                selectedModel = modelForSession,
                messages = emptyList(),
                isSpace = true,
                isRagEnabled = true,
                spaceFileNames = emptyList(),
                isWebSearchEnabled = false,
                isCodingEnabled = false
            )
        }
        Log.d(TAG, "Created space: $sessionId name='$name'")
        chatRepository.updateConversation(conversation)
        val modelDir = copyBgeModelIfNeeded()
        if (modelDir != null) {
            ZetlaCore.nativeInitRagModel(modelDir)
        }
        ZetlaCore.nativeSetRagConfig(configRepository.getRagConfig().toJson())
        chatRepository.setSessionRag(nativeSid, true)
        registerRagTools()
    }

    private fun registerRagTools() {
        val nativeSid = sessionId.replace("-", "")
        
        // rewrite_query tool (LLM-powered query optimization)
        val rewriteSchema = """{"type":"object","properties":{"query":{"type":"string","description":"Original query to rewrite for better document search"}},"required":["query"]}"""
        val rewriteDesc = "Rewrite a search query to be more specific with domain terminology, expanded abbreviations, and synonyms for better document retrieval."
        ZetlaCore.nativeAddTool(nativeSid, "rewrite_query", rewriteDesc, rewriteSchema)
        
        val rewriteExecutor = object : ToolExecutorCallback {
            override fun execute(sessionId: String, toolName: String, argumentsJson: String): String {
                try {
                    val obj = org.json.JSONObject(argumentsJson)
                    val query = obj.optString("query", "")
                    if (query.isBlank()) return """{"error":"Missing query"}"""
                    return """{"rewritten":"$query"}"""
                } catch (e: Exception) {
                    return """{"error":"${e.message?.replace("\"", "\\\"") ?: "Unknown"}"}"""
                }
            }
        }
        ZetlaCore.nativeSetToolExecutor(nativeSid, rewriteExecutor)
    }

    private suspend fun copyBgeModelIfNeeded(): String? = withContext(Dispatchers.IO) {
        try {
            val modelDir = java.io.File(appContext.filesDir, "bge_model")
            if (modelDir.exists() && java.io.File(modelDir, "word_embeds.bin").exists()) {
                return@withContext modelDir.absolutePath
            }
            modelDir.mkdirs()
            for (name in arrayOf("word_embeds.bin", "vocab.txt")) {
                val dest = java.io.File(modelDir, name)
                appContext.assets.open("bge_model/$name").use { input ->
                    dest.outputStream().use { output -> input.copyTo(output) }
                }
            }
            modelDir.absolutePath
        } catch (_: Exception) { null }
    }

    private suspend fun indexFileForRag(file: FileAttachment) {
        try {
            Log.d(TAG, "indexFileForRag: ${file.name} path=${file.path} type=${file.type} size=${file.size}")
            val text = withContext(Dispatchers.Default) {
                when (file.type) {
                    FileType.PDF -> {
                        Log.d(TAG, "indexFileForRag: extracting PDF ${file.name}")
                        fileRepository.extractPdfText(android.net.Uri.fromFile(java.io.File(file.path))).data
                    }
                    FileType.TEXT -> {
                        val f = java.io.File(file.path)
                        if (!f.exists()) { Log.w(TAG, "indexFileForRag: file not found ${file.path}"); null }
                        else {
                            Log.d(TAG, "indexFileForRag: reading text ${file.name} len=${f.length()}")
                            try { f.readText() } catch (e: Exception) {
                                Log.w(TAG, "indexFileForRag: cannot read ${file.name} as text: ${e.message}"); null
                            }
                        }
                    }
                    FileType.DOCUMENT, FileType.SPREADSHEET, FileType.PRESENTATION -> {
                        Log.d(TAG, "indexFileForRag: native extraction for ${file.name} (type=${file.type})")
                        try {
                            val json = ZetlaCore.nativeExtractFileText(file.path)
                            Log.d(TAG, "indexFileForRag: native result for ${file.name}: ${json.take(200)}")
                            val text = try {
                                org.json.JSONObject(json).optString("text", "")
                            } catch (e: Exception) { "" }
                            if (text.isNotBlank()) text else {
                                Log.w(TAG, "indexFileForRag: native extraction empty for ${file.name}")
                                null
                            }
                        } catch (e: Exception) {
                            Log.w(TAG, "indexFileForRag: native extraction failed for ${file.name}: ${e.message}")
                            null
                        }
                    }
                    FileType.IMAGE, FileType.UNKNOWN -> {
                        Log.w(TAG, "indexFileForRag: unsupported file type ${file.type} for ${file.name}, skipping")
                        null
                    }
                    else -> {
                        val f = java.io.File(file.path)
                        if (!f.exists()) {
                            Log.w(TAG, "indexFileForRag: file not found ${file.path}")
                            null
                        } else {
                            Log.d(TAG, "indexFileForRag: reading text ${file.name} len=${f.length()}")
                            try { f.readText() } catch (e: Exception) {
                                Log.w(TAG, "indexFileForRag: cannot read ${file.name} as text: ${e.message}")
                                null
                            }
                        }
                    }
                }
            }
            if (!text.isNullOrBlank()) {
                Log.d(TAG, "indexFileForRag: extracted ${text.length} chars for ${file.name}")
                val result = chatRepository.addSpaceFile(sessionId.replace("-", ""), file.path, text)
                Log.d(TAG, "indexFileForRag: addSpaceFile result=$result")
            } else {
                Log.w(TAG, "indexFileForRag: no text extracted for ${file.name} (type=${file.type})")
            }
        } catch (e: TimeoutCancellationException) {
            Log.w(TAG, "indexFileForRag: timeout for ${file.name}, skipping")
        } catch (e: Exception) {
            Log.e(TAG, "indexFileForRag failed for ${file.name}", e)
        }
    }

    private suspend fun refreshSpaceFiles() {
        try {
            val nativeSid = sessionId.replace("-", "")
            val files = chatRepository.listSpaceFiles(nativeSid)
            Log.d(TAG, "refreshSpaceFiles: nativeSid=$nativeSid files=$files")
            _uiState.update { it.copy(spaceFileNames = files) }
        } catch (e: Exception) {
            Log.e(TAG, "refreshSpaceFiles failed", e)
        }
    }
    
    private suspend fun reindexAllSpaceFiles() {
        try {
            val files = _uiState.value.spaceFileNames
            for (filePath in files) {
                val f = java.io.File(filePath)
                if (!f.exists()) continue
                val name = f.name
                val text: String? = if (name.endsWith(".pdf")) {
                    fileRepository.extractPdfText(android.net.Uri.fromFile(f)).data
                } else {
                    f.readText()
                }
                if (!text.isNullOrBlank()) {
                    chatRepository.addSpaceFile(sessionId.replace("-", ""), filePath, text)
                }
            }
            if (files.isNotEmpty()) {
                Log.d(TAG, "Reindexed ${files.size} files for space $sessionId")
            }
        } catch (e: Exception) {
            Log.w(TAG, "Reindex failed: ${e.message}")
        }
    }
    
    private suspend fun reindexFromPath(path: String, name: String) = reindexAllSpaceFiles()

    private suspend fun selectConversation(conversation: Conversation) {
        val sid = conversation.id.toString()
        Log.d(TAG, "selectConversation: $sid isSpace=${conversation.isSpace}")
        sessionId = sid
        _uiState.update {
            it.copy(
                selectedConversation = conversation,
                selectedModel = conversation.selectedModel,
                messages = emptyList(),
                isSpace = conversation.isSpace
            )
        }
        val loaded = chatRepository.loadSession(sid)
        Log.d(TAG, "selectConversation: loaded=$loaded isSpace=${conversation.isSpace}")
        if (loaded && conversation.isSpace) {
            Log.d(TAG, "selectConversation: restoring space session")
            val nativeSid = sid.replace("-", "")
            refreshSpaceFiles()
            val modelDir = copyBgeModelIfNeeded()
            if (modelDir != null) {
                ZetlaCore.nativeInitRagModel(modelDir)
            }
            // Load RAG index from disk (fast - just Lorentz vectors + chunk texts)
            val ragDir = java.io.File(appContext.filesDir, "rag_$nativeSid")
            val loadedRag = if (ragDir.exists()) {
                ZetlaCore.nativeLoadRagSession(nativeSid, ragDir.absolutePath)
            } else false
            Log.d(TAG, "selectConversation: RAG load from disk=$loadedRag")
            _uiState.update { it.copy(isRagEnabled = true) }
            chatRepository.setSessionRag(nativeSid, true)
            registerRagTools()
            Log.d(TAG, "selectConversation: space restored, tools registered")
        }
        Log.d(TAG, "loadSession result: $loaded")
        if (loaded) {
            val history = chatRepository.getMessages(conversation.id)
            Log.d(TAG, "loadMessages count: ${history.size}")
            _uiState.update {
                it.copy(
                    messages = history.filter { it.role != Role.SYSTEM }.map { msg ->
                        UiMessage(
                            id = UUID.randomUUID().toString(),
                            content = msg.content,
                            isUser = msg.role == Role.USER
                        )
                    }
                )
            }
        }
    }

    private suspend fun deleteConversation(conversation: Conversation) {
        Log.d(TAG, "deleteConversation: ${conversation.id}")
        chatRepository.deleteMessages(conversation.id)
        chatRepository.deleteConversation(conversation.id)
        _uiState.update { it.copy(messages = emptyList(), selectedConversation = null) }
    }

    private suspend fun loadHistory() {
        val history = chatRepository.getHistory(sessionId)
        Log.d(TAG, "loadHistory: session=$sessionId messages=${history.size}")
        if (history.isNotEmpty()) {
            _uiState.update {
                it.copy(
                    messages = history.filter { it.role != Role.SYSTEM }.map { msg ->
                        UiMessage(
                            id = UUID.randomUUID().toString(),
                            content = msg.content,
                            isUser = msg.role == Role.USER
                        )
                    }
                )
            }
        }
    }
}
