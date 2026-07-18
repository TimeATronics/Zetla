package com.zetla.ui.voice

import android.app.Application
import android.speech.tts.TextToSpeech
import android.util.Log
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.zetla.domain.model.ChatMessage
import com.zetla.domain.model.Model
import com.zetla.domain.model.Role
import com.zetla.domain.repository.ChatRepository
import com.zetla.domain.repository.ConfigRepository
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.json.JSONObject
import org.vosk.android.RecognitionListener
import java.util.UUID
import javax.inject.Inject

private const val TAG = "VoiceChatVM"

data class VoiceChatUiState(
    val isListening: Boolean = false,
    val isProcessing: Boolean = false,
    val isSpeaking: Boolean = false,
    val partialText: String = "",
    val transcript: String = "",
    val responseText: String = "",
    val error: String? = null,
    val modelLoaded: Boolean = false,
    val isModelLoading: Boolean = true,
    val sessionId: String = "",
    val messages: List<ChatMessage> = emptyList(),
    val selectedModel: Model = Model("", ""),
    val models: List<Model> = emptyList(),
    val isWebSearchEnabled: Boolean = false,
    val isCodingEnabled: Boolean = false,
    val reasoningEffort: String = ""
)

@HiltViewModel
class VoiceChatViewModel @Inject constructor(
    private val application: Application,
    private val chatRepository: ChatRepository,
    private val configRepository: ConfigRepository
) : ViewModel(), TextToSpeech.OnInitListener {

    private val _uiState = MutableStateFlow(VoiceChatUiState())
    val uiState: StateFlow<VoiceChatUiState> = _uiState

    private var voiceService: VoiceRecognitionService? = null
    private var tts: TextToSpeech? = null
    private var ttsInitialized = false
    private var sessionId: String = ""
    private var isProcessingTranscript = false
    private val transcriptMutex = Mutex()

    private val _snackbarEvent = MutableSharedFlow<String>(extraBufferCapacity = 2)
    val snackbarEvent: SharedFlow<String> = _snackbarEvent.asSharedFlow()

    init {
        voiceService = VoiceRecognitionService(application)
        tts = TextToSpeech(application, this)
        viewModelScope.launch(Dispatchers.IO) {
            loadModels()
            val apiKey = configRepository.getApiKey()
            if (apiKey.isNotBlank()) createSession()
        }
        viewModelScope.launch(Dispatchers.IO) {
            loadModelBackground()
        }
    }

    override fun onInit(status: Int) {
        if (status == TextToSpeech.SUCCESS) {
            ttsInitialized = true
            restoreTtsVoice()
            Log.d(TAG, "TTS initialized")
        }
    }

    private fun restoreTtsVoice() {
        val savedVoiceName = configRepository.getTtsVoice()
        if (savedVoiceName.isNotBlank()) {
            val voice = tts?.voices?.find { it.name == savedVoiceName }
            if (voice != null) {
                tts?.setVoice(voice)
                Log.d(TAG, "Restored TTS voice: $savedVoiceName")
            }
        }
    }

    private suspend fun loadModels() {
        val saved = configRepository.getModel()
        val savedProvider = configRepository.getProvider()
        val models = configRepository.fetchAllProviderModels()
        val selected = models.find { it.id == saved } ?: models.firstOrNull() ?: Model("", "")
        if (selected.provider.isNotEmpty() && selected.provider != savedProvider) {
            configRepository.setProvider(selected.provider)
        }
        _uiState.update { it.copy(models = models, selectedModel = selected) }
    }

    private fun createSession() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val userPrompt = configRepository.getSystemPrompt()
                val voicePrompt = """
                    You are in a voice conversation. The user's speech was converted to text
                    by automatic speech recognition and may contain transcription errors
                    (e.g. homophones, missing punctuation, incorrect words). Be tolerant of
                    such errors, ask for clarification if needed, and answer naturally.
                    Keep responses concise and suitable for spoken delivery (easy to read aloud).
                    Do not use markdown formatting, emojis, or complex symbols.
                """.trimIndent()
                val now = java.text.SimpleDateFormat("EEEE, MMMM d, yyyy", java.util.Locale.US).format(java.util.Date())
                val datePrompt = "Current date: $now."
                val combinedPrompt = buildString {
                    if (userPrompt.isNotBlank() && !userPrompt.contains("voice conversation")) {
                        append(userPrompt)
                        append("\n\n")
                    }
                    append(voicePrompt)
                    append("\n\n")
                    append(datePrompt)
                }

                val model = _uiState.value.selectedModel
                val modelStr = if (model.id.isNotBlank()) model.id else configRepository.getModel()
                    .ifEmpty { "deepseek-v4-flash" }

                val conversation = chatRepository.createConversation(
                    title = "Voice Chat",
                    model = Model(modelStr, modelStr),
                    systemPrompt = combinedPrompt
                )
                sessionId = conversation.id.toString()
                _uiState.update { it.copy(sessionId = sessionId) }
            } catch (e: Exception) {
                _uiState.update { it.copy(error = "Failed to create session: ${e.message}") }
            }
        }
    }

    fun selectModel(model: Model) {
        _uiState.update { it.copy(selectedModel = model) }
        configRepository.setModel(model.id)
        if (model.provider.isNotEmpty()) {
            configRepository.setProvider(model.provider)
        }
    }

    fun retryLoadModels() {
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.update { it.copy(isModelLoading = true) }
            loadModels()
            _uiState.update { it.copy(isModelLoading = false) }
        }
    }

    private suspend fun loadModelBackground() {
        voiceService?.loadModel(object : VoiceRecognitionService.VoiceCallback {
            override fun onPartialResult(text: String) {
                _uiState.update { it.copy(partialText = text) }
            }
            override fun onResult(text: String) {
                _uiState.update { it.copy(transcript = text, partialText = "") }
            }
            override fun onError(error: String) {
                _uiState.update { it.copy(error = error, isModelLoading = false) }
            }
            override fun onReady() {
                _uiState.update { it.copy(modelLoaded = true, isModelLoading = false) }
            }
        })
    }

    fun loadModel() {
        if (_uiState.value.isModelLoading) return
        _uiState.update { it.copy(isModelLoading = true) }
        voiceService?.loadModel(object : VoiceRecognitionService.VoiceCallback {
            override fun onPartialResult(text: String) {
                _uiState.update { it.copy(partialText = text) }
            }
            override fun onResult(text: String) {
                _uiState.update { it.copy(transcript = text, partialText = "") }
            }
            override fun onError(error: String) {
                _uiState.update { it.copy(error = error, isModelLoading = false) }
            }
            override fun onReady() {
                _uiState.update { it.copy(modelLoaded = true, isModelLoading = false) }
            }
        })
    }

    fun startListening() {
        if (voiceService?.isListening() == true || isProcessingTranscript) return

        _uiState.update { it.copy(
            isListening = true,
            partialText = "",
            transcript = "",
            responseText = ""
        )}

        voiceService?.startListening(object : RecognitionListener {
            override fun onPartialResult(hypothesis: String?) {
                hypothesis?.let { text ->
                    val partial = extractTextFromJson(text)
                    if (partial.isNotBlank()) {
                        _uiState.update { it.copy(partialText = partial) }
                    }
                }
            }
            override fun onResult(hypothesis: String?) {
                hypothesis?.let { text ->
                    val final = extractTextFromJson(text)
                    if (final.isNotBlank()) {
                        val existing = _uiState.value.transcript
                        val appended = if (existing.isNotBlank()) "$existing $final" else final
                        _uiState.update { it.copy(partialText = "", transcript = appended) }
                    }
                }
            }
            override fun onFinalResult(hypothesis: String?) {
                hypothesis?.let { text ->
                    val final = extractTextFromJson(text)
                    if (final.isNotBlank()) {
                        val existing = _uiState.value.transcript
                        val appended = if (existing.isNotBlank()) "$existing $final" else final
                        _uiState.update { it.copy(
                            partialText = "",
                            transcript = appended,
                            isListening = false
                        )}
                        processTranscript(appended)
                    }
                }
            }
            override fun onError(exception: Exception?) {
                Log.e(TAG, "Recognition error", exception)
                _uiState.update { it.copy(isListening = false,
                    error = "Recognition error: ${exception?.message}") }
            }
            override fun onTimeout() {
                _uiState.update { it.copy(isListening = false) }
            }
        })
    }

    fun stopListening() {
        voiceService?.stopListening()
        val finalText = _uiState.value.transcript
        _uiState.update { it.copy(isListening = false) }
        if (finalText.isNotBlank() && !isProcessingTranscript) {
            processTranscript(finalText)
        }
    }

    fun toggleWebSearch() {
        val newEnabled = !_uiState.value.isWebSearchEnabled
        _uiState.update { it.copy(isWebSearchEnabled = newEnabled) }
        _snackbarEvent.tryEmit(if (newEnabled) "Web search enabled" else "Web search disabled")
        viewModelScope.launch {
            if (sessionId.isNotBlank()) {
                chatRepository.setSessionWebSearch(sessionId, newEnabled)
                rebuildSessionSystemPrompt()
            }
        }
    }

    fun toggleCoding() {
        val newEnabled = !_uiState.value.isCodingEnabled
        _uiState.update { it.copy(isCodingEnabled = newEnabled) }
        _snackbarEvent.tryEmit(if (newEnabled) "Coding tools enabled" else "Coding tools disabled")
        viewModelScope.launch {
            if (sessionId.isNotBlank()) {
                rebuildSessionSystemPrompt()
            }
        }
    }

    private suspend fun rebuildSessionSystemPrompt() {
        if (sessionId.isBlank()) return
        val state = _uiState.value
        val userPrompt = configRepository.getSystemPrompt()
        val toolParts = mutableListOf<String>()
        if (state.isWebSearchEnabled) {
            toolParts.add("""
You have access to the web_search tool with two modes:
  1. mode='search' + query -> get search results (titles, URLs, snippets)
  2. mode='fetch' + url -> get the full content of a web page

When to use each mode:
- "who won the latest match" -> use search mode
- "what's the weather in Paris" -> use search mode
- "I need the full article from this link https://..." -> use fetch mode to read the entire page
- "summarize this page: https://..." -> use fetch mode
- "latest news about AI" -> use search, then fetch promising URLs for deeper info

The search tool returns titles, URLs and snippets. Use fetch mode with specific URLs to get complete page content.
""".trimIndent())
        }
        if (state.isCodingEnabled) {
            toolParts.add("""
You have access to the run_code tool (Python, standard library only). Use it for computation, data processing, and verification.

When to use run_code:
- "calculate 15% tip on $47.50" -> compute the exact amount
- "what's 2^10" -> compute powers
- "convert 100 USD to EUR at rate 0.92" -> do the conversion
- "sort these numbers: 42, 7, 19, 3" -> run a quick sort
- "is 97 prime?" -> check primality

Available modules: math, decimal, fractions, random, json, re, collections, itertools, functools.
No pip, no internet, no filesystem access.
""".trimIndent())
        }
        val voiceBase = "You are in a voice conversation. The user's speech was converted to text by automatic speech recognition and may contain transcription errors. Be tolerant of such errors, ask for clarification if needed, and answer naturally. Keep responses concise and suitable for spoken delivery. Do not use markdown formatting, emojis, or complex symbols."
        val now = java.text.SimpleDateFormat("EEEE, MMMM d, yyyy", java.util.Locale.US).format(java.util.Date())
        val prompt = buildString {
            if (userPrompt.isNotBlank()) {
                append(userPrompt); append("\n\n")
            }
            append(voiceBase)
            if (toolParts.isNotEmpty()) {
                append("\n\n"); append(toolParts.joinToString("\n"))
            }
            append("\n\nCurrent date: $now.")
        }
        chatRepository.setSessionSystemPrompt(sessionId, prompt)
    }

    private fun processTranscript(text: String) {
        if (sessionId.isBlank() || isProcessingTranscript || text.isBlank()) return
        isProcessingTranscript = true

        viewModelScope.launch(Dispatchers.IO) {
            transcriptMutex.withLock {
                isProcessingTranscript = true
                _uiState.update { it.copy(isProcessing = true, responseText = "") }

                try {
                    _uiState.update { state ->
                        state.copy(messages = state.messages + ChatMessage(role = Role.USER, content = text))
                    }

                    val model = _uiState.value.selectedModel
                    val modelId = if (model.id.isNotBlank()) model.id else configRepository.getModel()

                    val params = configRepository.getDefaultParams()
                    if (sessionId.isNotBlank()) {
                        chatRepository.setSessionOptions(sessionId, params.toOptionsJson())
                    }

                    val response = StringBuilder()
                    chatRepository.sendMessageFull(
                        sessionId = sessionId,
                        message = text,
                        model = modelId,
                        onToken = { content, _ ->
                            if (content != null) {
                                response.append(content)
                                _uiState.update { it.copy(responseText = stripMarkdownForTts(response.toString())) }
                            }
                        },
                        onFinished = {
                            val finalResponse = stripMarkdownForTts(response.toString())
                            if (finalResponse.isNotBlank()) {
                                _uiState.update { state ->
                                    state.copy(
                                        messages = state.messages + ChatMessage(role = Role.ASSISTANT, content = finalResponse),
                                        isProcessing = false,
                                        responseText = ""
                                    )
                                }
                                speakResponse(finalResponse)
                            } else {
                                _uiState.update { it.copy(isProcessing = false) }
                            }
                        }
                    )
                } catch (e: Exception) {
                    _uiState.update { it.copy(isProcessing = false, error = "Failed: ${e.message}") }
                } finally {
                    isProcessingTranscript = false
                }
            }
        }
    }

    private fun speakResponse(text: String) {
        if (!ttsInitialized || text.isBlank()) return
        _uiState.update { it.copy(isSpeaking = true) }

        val utteranceId = UUID.randomUUID().toString()
        tts?.setOnUtteranceProgressListener(object : android.speech.tts.UtteranceProgressListener() {
            override fun onStart(id: String?) { _uiState.update { it.copy(isSpeaking = true) } }
            override fun onDone(id: String?) { _uiState.update { it.copy(isSpeaking = false) } }
            @Deprecated("Deprecated")
            override fun onError(id: String?) { _uiState.update { it.copy(isSpeaking = false) } }
        })

        tts?.speak(text, TextToSpeech.QUEUE_FLUSH, null, utteranceId)
    }

    fun interruptSpeech() {
        tts?.stop()
        _uiState.update { it.copy(isSpeaking = false) }
    }

    fun stopResponse() {
        chatRepository.cancelRequest()
        tts?.stop()
        isProcessingTranscript = false
        _uiState.update { it.copy(isProcessing = false, isSpeaking = false, isListening = false) }
    }

    fun clearError() {
        _uiState.update { it.copy(error = null) }
    }

    private fun extractTextFromJson(json: String): String {
        return try {
            JSONObject(json).optString("text", "")
        } catch (_: Exception) {
            json
        }
    }

    private fun stripMarkdownForTts(text: String): String {
        return text
            .replace(Regex("```[\\s\\S]*?```"), "")
            .replace(Regex("`([^`]+)`"), "$1")
            .replace(Regex("\\*\\*\\*(.+?)\\*\\*\\*"), "$1")
            .replace(Regex("___(.+?)___"), "$1")
            .replace(Regex("\\*\\*(.+?)\\*\\*"), "$1")
            .replace(Regex("__(.+?)__"), "$1")
            .replace(Regex("\\*(.+?)\\*"), "$1")
            .replace(Regex("_(.+?)_"), "$1")
            .replace(Regex("~~(.+?)~~"), "$1")
            .replace(Regex("\\[([^\\]]+)\\]\\([^)]+\\)"), "$1")
            .replace(Regex("^#{1,6}\\s+", RegexOption.MULTILINE), "")
            .replace(Regex("^\\s*[-*+]>\\s+", RegexOption.MULTILINE), "")
            .replace(Regex("^\\s*[-*+]\\s+", RegexOption.MULTILINE), "")
            .replace(Regex("^\\s*\\d+\\.\\s+", RegexOption.MULTILINE), "")
            .replace(Regex("^\\s*[-_*]{3,}\\s*$", RegexOption.MULTILINE), "")
            .replace(Regex("\\|"), "")
            .trim()
    }

    override fun onCleared() {
        super.onCleared()
        voiceService?.destroy()
        tts?.stop()
        tts?.shutdown()
    }
}
