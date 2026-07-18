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

private const val TAG = "ChatViewModel"

@HiltViewModel
class ChatViewModel @Inject constructor(
    savedStateHandle: SavedStateHandle,
    private val chatRepository: ChatRepository,
    private val configRepository: ConfigRepository,
    private val fileRepository: FileRepository
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
        val dynamicPrompt = buildDynamicPrompt(userPrompt, _uiState.value.isWebSearchEnabled, _uiState.value.isCodingEnabled, now)
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
                if (_uiState.value.attachedFiles.isEmpty()) {
                    _uiState.update { it.copy(attachedFiles = listOf(event.file)) }
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
        }
    }

    private fun buildDynamicPrompt(userPrompt: String, isWebSearch: Boolean, isCoding: Boolean, dateStr: String): String {
        val toolParts = mutableListOf<String>()
        if (isWebSearch) {
            toolParts.add("""
You have access to the web_search tool with two modes:
  1. mode='search' + query → get search results (titles, URLs, snippets)
  2. mode='fetch' + url → get the full content of a web page

When to use each mode:
• "who won the latest match" → use search mode
• "what's the weather in Paris" → use search mode
• "I need the full article from this link https://..." → use fetch mode to read the entire page
• "summarize this page: https://..." → use fetch mode
• "latest news about AI" → use search, then fetch promising URLs for deeper info

The search tool returns titles, URLs and snippets. Use fetch mode with specific URLs to get complete page content.
""".trimIndent())
        }
        if (isCoding) {
            toolParts.add("""
You have access to the run_code tool (Python, standard library only). Use it for computation, data processing, and verification.

When to use run_code:
• "calculate 15% tip on $47.50" → compute the exact amount
• "what's 2^10" → compute powers
• "convert 100 USD to EUR at rate 0.92" → do the conversion
• "sort these numbers: 42, 7, 19, 3" → run a quick sort
• "is 97 prime?" → check primality

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

        // Auto-title: set title from first user message
        if (!hasSentMessage) {
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

            try {
                withTimeout(90_000L) {
                    val modelId = _uiState.value.selectedModel.id.ifEmpty { configRepository.getModel() }
                    Log.d(TAG, "sendMessage: session=$sessionId model=$modelId text='$finalText' pdfFiles=${pdfFiles.size} nonPdfFiles=${nonPdfFiles.size} imageUris=${imageUris.size}")

                    val params = configRepository.getDefaultParams()
                    chatRepository.setSessionOptions(sessionId, params.toOptionsJson())

                    val state = _uiState.value
                    val userPrompt = configRepository.getSystemPrompt()
                    val now = java.text.SimpleDateFormat("EEEE, MMMM d, yyyy", java.util.Locale.US).format(java.util.Date())
                    val dynamicPrompt = buildDynamicPrompt(userPrompt, state.isWebSearchEnabled, state.isCodingEnabled, now)
                    chatRepository.setSessionSystemPrompt(sessionId, dynamicPrompt)

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
                    messages = emptyList()
                )
            }
        }
    }

    private suspend fun selectConversation(conversation: Conversation) {
        val sid = conversation.id.toString()
        Log.d(TAG, "selectConversation: $sid")
        sessionId = sid
        _uiState.update {
            it.copy(
                selectedConversation = conversation,
                selectedModel = conversation.selectedModel,
                messages = emptyList()
            )
        }
        val loaded = chatRepository.loadSession(sid)
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
