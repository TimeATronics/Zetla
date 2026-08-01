package com.zetla.ui.screens.settings

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.zetla.data.ZetlaCore
import com.zetla.data.RagDebugCallback
import com.zetla.domain.model.FileAttachment
import com.zetla.domain.model.FileType
import com.zetla.domain.repository.ConfigRepository
import com.zetla.domain.repository.FileRepository
import com.zetla.ui.theme.AppColorScheme
import dagger.hilt.android.lifecycle.HiltViewModel
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.io.File
import javax.inject.Inject

private const val TAG = "SettingsViewModel"
private const val RAG_SESSION = "settings_rag_test"

@HiltViewModel
class SettingsViewModel @Inject constructor(
    private val configRepository: ConfigRepository,
    private val fileRepository: FileRepository,
    @ApplicationContext private val appContext: Context
) : ViewModel() {

    private val _uiState = MutableStateFlow(SettingsUiState.Default)
    val uiState: StateFlow<SettingsUiState> = _uiState.asStateFlow()

    private var ragInitialized = false

    private val ragDebugCallback = object : RagDebugCallback {
        override fun onDebug(message: String) {
            _uiState.update {
                val current = it.ragDebugLog
                val newLog = if (current.length > 10000) current.substring(current.length - 5000) else current
                it.copy(ragDebugLog = newLog + message + "\n")
            }
        }
    }

    init {
        loadSettings()
        // RAG test disabled - initRag()
    }

    private fun initRag() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val modelDir = copyModelFromAssets()
                ZetlaCore.nativeRagInit(modelDir)
                ZetlaCore.nativeRagSetDebugCallback(ragDebugCallback)
                ragInitialized = true
                logRag("RAG initialized (BGE-small-en via ORT)")
            } catch (e: Exception) {
                Log.e(TAG, "RAG init failed", e)
                logRag("RAG init failed: ${e.message}")
            }
        }
    }

    private fun copyModelFromAssets(): String {
        return try {
            // Copy from assets/bge_model/ to app files dir
            val modelDir = java.io.File(appContext.filesDir, "bge_model")
            if (modelDir.exists() && java.io.File(modelDir, "word_embeds.bin").exists()) {
                return modelDir.absolutePath
            }
            modelDir.mkdirs()
            val assets = arrayOf("word_embeds.bin", "vocab.txt")
            for (name in assets) {
                val dest = java.io.File(modelDir, name)
                appContext.assets.open("bge_model/$name").use { input ->
                    dest.outputStream().use { output -> input.copyTo(output) }
                }
            }
            logRag("Model copied to: ${modelDir.absolutePath}")
            modelDir.absolutePath
        } catch (e: Exception) {
            Log.e(TAG, "Failed to copy model", e)
            "settings_stub"
        }
    }

    private fun logRag(msg: String) {
        _uiState.update {
            val current = it.ragDebugLog
            val newLog = if (current.length > 10000) current.substring(current.length - 5000) else current
            it.copy(ragDebugLog = newLog + msg + "\n")
        }
    }

    fun onUiEvent(event: SettingsUiEvent) {
        when (event) {
            is SettingsUiEvent.LoadSettings -> loadSettings()
            is SettingsUiEvent.ToggleDarkMode -> toggleDarkMode()
            is SettingsUiEvent.SaveAllSettings -> saveAllSettings()
            is SettingsUiEvent.DismissError -> _uiState.update { it.copy(error = null) }
            is SettingsUiEvent.DismissSavedMessage -> _uiState.update { it.copy(savedMessage = null) }
            is SettingsUiEvent.SetSystemPromptDraft -> {
                _uiState.update { it.copy(systemPromptDraft = event.prompt) }
            }
            is SettingsUiEvent.SaveSystemPrompt -> {
                configRepository.setSystemPrompt(_uiState.value.systemPromptDraft)
                _uiState.update { it.copy(
                    systemPrompt = _uiState.value.systemPromptDraft,
                    savedMessage = "System prompt saved"
                )}
            }
            is SettingsUiEvent.RestoreDefaultSystemPrompt -> {
                val default = ConfigRepository.DEFAULT_SYSTEM_PROMPT
                configRepository.setSystemPrompt(default)
                _uiState.update { it.copy(
                    systemPrompt = default,
                    systemPromptDraft = default,
                    savedMessage = "System prompt restored"
                )}
            }
            is SettingsUiEvent.SaveProviderConfigDirect -> {
                configRepository.setProviderConfig(event.providerId, event.apiKey, event.baseUrl, event.enabled)
                if (event.apiKey.isNotBlank()) {
                    configRepository.setApiKey(event.apiKey)
                    configRepository.setProvider(event.providerId)
                }
                loadSettings()
                _uiState.update { it.copy(savedMessage = "${event.providerId} configured") }
            }
            is SettingsUiEvent.RefetchModels -> {
                _uiState.update { it.copy(isRefetchingModels = true) }
                viewModelScope.launch(Dispatchers.IO) {
                    val ok = configRepository.refreshModelsDevCache(true)
                    _uiState.update {
                        it.copy(
                            isRefetchingModels = false,
                            savedMessage = if (ok) "Models cache refreshed" else "Failed to refresh models"
                        )
                    }
                }
            }
            is SettingsUiEvent.SelectColorScheme -> {
                configRepository.setColorScheme(event.scheme.id)
                _uiState.update { it.copy(colorScheme = event.scheme) }
            }
            is SettingsUiEvent.SelectTtsVoice -> {
                configRepository.setTtsVoice(event.voiceName)
                _uiState.update { it.copy(selectedTtsVoiceName = event.voiceName) }
            }
            is SettingsUiEvent.SetTtsVoices -> {
                _uiState.update { it.copy(ttsVoices = event.voices) }
            }
            is SettingsUiEvent.AppendRagDebugLog -> {
                val current = _uiState.value.ragDebugLog
                val newLog = if (current.length > 10000) current.substring(current.length - 5000) else current
                _uiState.update { it.copy(ragDebugLog = newLog + event.message + "\n") }
            }
            is SettingsUiEvent.AttachRagFile -> {
                val files = _uiState.value.ragAttachedFiles
                // Clear stale session data on first file of a new test
                if (files.isEmpty()) {
                    ZetlaCore.nativeRagRemoveSession(RAG_SESSION)
                    logRag("Session cleared for new test run")
                }
                _uiState.update { it.copy(ragAttachedFiles = files + event.file) }
                indexFile(event.file)
            }
            is SettingsUiEvent.RemoveRagFile -> {
                val files = _uiState.value.ragAttachedFiles.filter { it.id != event.fileId }
                _uiState.update { it.copy(ragAttachedFiles = files) }
            }
            is SettingsUiEvent.SetRagQuery -> {
                _uiState.update { it.copy(ragQueryText = event.query) }
            }
            is SettingsUiEvent.ExecuteRagSearch -> executeRagSearch()
            is SettingsUiEvent.SetRagRerank -> {
                _uiState.update { it.copy(ragRerankEnabled = event.enabled) }
                saveRagConfig()
            }
            is SettingsUiEvent.SetRagProjection -> {
                _uiState.update { it.copy(ragProjectionEnabled = event.enabled) }
                saveRagConfig()
            }
            is SettingsUiEvent.SetRagBm25Alpha -> {
                _uiState.update { it.copy(ragBm25Alpha = event.value) }
                saveRagConfig()
            }
            is SettingsUiEvent.SetRagChunkChars -> {
                _uiState.update { it.copy(ragChunkChars = event.value) }
                saveRagConfig()
            }
            is SettingsUiEvent.SetRagOverlapChars -> {
                _uiState.update { it.copy(ragOverlapChars = event.value) }
                saveRagConfig()
            }
        }
    }

    private fun indexFile(file: FileAttachment) {
        if (!ragInitialized) {
            logRag("RAG not initialized - cannot index file")
            return
        }
        _uiState.update { it.copy(ragIsIndexing = true) }
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val text = if (file.type == FileType.PDF) {
                    extractPdfText(file)
                } else {
                    readFileText(file.path, file.name)
                }
                if (text.isNullOrBlank()) {
                    logRag("Cannot read text from: ${file.name}")
                    _uiState.update { it.copy(ragIsIndexing = false) }
                    return@launch
                }
                logRag("Indexing: ${file.name} (${text.length} chars)")
                val result = ZetlaCore.nativeRagAddFile(RAG_SESSION, file.path, text)
                logRag("Index result: $result")
                _uiState.update {
                    it.copy(
                        ragIsIndexing = false,
                        ragSearchResult = "Indexed: ${file.name} -> $result chunks, total: ${ZetlaCore.nativeRagChunkCount(RAG_SESSION)}"
                    )
                }
            } catch (e: Exception) {
                Log.e(TAG, "Index file failed", e)
                logRag("Index error: ${e.message}")
                _uiState.update { it.copy(ragIsIndexing = false) }
            }
        }
    }

    private suspend fun extractPdfText(file: FileAttachment): String? {
        return try {
            val uri = Uri.fromFile(File(file.path))
            val result = fileRepository.extractPdfText(uri)
            val data = result.data
            if (result.success && data != null) {
                logRag("PDF extracted: ${file.name} (${data.length} chars)")
                data
            } else {
                logRag("PDF extraction failed: ${file.name}: ${result.error}")
                null
            }
        } catch (e: Exception) {
            logRag("PDF extraction exception: ${e.message}")
            null
        }
    }

    private fun executeRagSearch() {
        val query = _uiState.value.ragQueryText
        if (query.isBlank()) return
        _uiState.update { it.copy(ragIsSearching = true) }
        viewModelScope.launch(Dispatchers.IO) {
            try {
                logRag("Search: \"$query\"")
                val json = ZetlaCore.nativeRagSearch(RAG_SESSION, query, 5, null)
                logRag("Search result: $json")
                _uiState.update {
                    it.copy(
                        ragIsSearching = false,
                        ragSearchResult = formatRagResults(json)
                    )
                }
            } catch (e: Exception) {
                Log.e(TAG, "RAG search failed", e)
                logRag("Search error: ${e.message}")
                _uiState.update { it.copy(ragIsSearching = false, ragSearchResult = "Error: ${e.message}") }
            }
        }
    }

    private fun formatRagResults(json: String): String {
        if (json == "[]") return "No results found."
        return try {
            val sb = StringBuilder()
            val cleaned = json.replace("\\\"", "\"").replace("\\n", "\n")
            var idx = 1
            val pathRegex = "\"path\":\"([^\"]+)\"".toRegex()
            val scoreRegex = "\"score\":([0-9.-]+)".toRegex()
            val textRegex = "\"text\":\"([^\"]*)\"".toRegex() // simplified
            val chunkIdxRegex = "\"chunk_idx\":([0-9]+)".toRegex()

            val paths = pathRegex.findAll(cleaned).map { it.groupValues[1] }.toList()
            val scores = scoreRegex.findAll(cleaned).map { it.groupValues[1] }.toList()
            val texts = textRegex.findAll(cleaned).map { it.groupValues[1] }.toList()
            val chunkIndices = chunkIdxRegex.findAll(cleaned).map { it.groupValues[1] }.toList()

            for (i in paths.indices) {
                val score = scores.getOrElse(i) { "?" }
                val text = texts.getOrElse(i) { "" }
                val ci = chunkIndices.getOrElse(i) { "?" }
                val shortPath = paths[i].substringAfterLast('/')
                sb.append("[${i + 1}] $shortPath #$ci (score=${score})\n")
                if (text.isNotEmpty()) sb.append("    ${text.take(200)}\n")
                sb.append("\n")
            }
            sb.toString().ifEmpty { json }
        } catch (e: Exception) {
            json
        }
    }

    private fun readFileText(path: String, fileName: String): String? {
        val file = File(path)
        if (!file.exists()) return null
        val ext = fileName.substringAfterLast('.', "").lowercase()
        return when (ext) {
            "txt", "md", "csv", "json", "xml", "html", "htm", "log", "cfg", "ini", "yaml", "yml",
            "py", "js", "ts", "kt", "java", "cpp", "c", "h", "hpp", "rs", "go", "swift" -> {
                try { file.readText() } catch (e: Exception) {
                    logRag("readText failed for $fileName: ${e.message}")
                    null
                }
            }
            else -> {
                logRag("Unsupported file type for RAG: .$ext - use .txt, .md, .csv, etc.")
                null
            }
        }
    }

    private fun loadSettings() {
        val providers = configRepository.listProviders()
        val systemPrompt = configRepository.getSystemPrompt()
        val providerConfigs = configRepository.listProviderConfigs()
        val ragConfig = configRepository.getRagConfig()
        _uiState.update {
            it.copy(
                providers = providers,
                isDarkMode = configRepository.isDarkMode(),
                colorScheme = AppColorScheme.fromId(configRepository.getColorScheme()),
                version = configRepository.getVersion(),
                systemPrompt = systemPrompt,
                systemPromptDraft = systemPrompt,
                providerConfigs = providerConfigs,
                selectedTtsVoiceName = configRepository.getTtsVoice(),
                ragRerankEnabled = ragConfig.rerankEnabled,
                ragProjectionEnabled = ragConfig.projectionEnabled,
                ragBm25Alpha = ragConfig.bm25Alpha,
                ragChunkChars = ragConfig.chunkChars,
                ragOverlapChars = ragConfig.overlapChars
            )
        }
    }

    private fun saveRagConfig() {
        val state = _uiState.value
        val config = com.zetla.domain.repository.RagConfig(
            bm25Alpha = state.ragBm25Alpha,
            projectionEnabled = state.ragProjectionEnabled,
            rerankEnabled = state.ragRerankEnabled,
            chunkChars = state.ragChunkChars,
            overlapChars = state.ragOverlapChars
        )
        configRepository.setRagConfig(config)
    }

    private fun toggleDarkMode() {
        val newValue = !_uiState.value.isDarkMode
        configRepository.setDarkMode(newValue)
        _uiState.update { it.copy(isDarkMode = newValue) }
    }

    private fun saveAllSettings() {
        configRepository.setSystemPrompt(_uiState.value.systemPromptDraft)
        _uiState.update { it.copy(
            systemPrompt = _uiState.value.systemPromptDraft,
            savedMessage = "All settings saved"
        )}
    }
}
