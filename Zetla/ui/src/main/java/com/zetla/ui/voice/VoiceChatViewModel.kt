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
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
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
    val sessionId: String = "",
    val messages: List<ChatMessage> = emptyList()
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

    init {
        voiceService = VoiceRecognitionService(application)
        tts = TextToSpeech(application, this)
        viewModelScope.launch {
            val apiKey = configRepository.getApiKey()
            if (apiKey.isNotBlank()) createSession()
        }
    }

    override fun onInit(status: Int) {
        if (status == TextToSpeech.SUCCESS) {
            ttsInitialized = true
            Log.d(TAG, "TTS initialized")
        }
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

                var modelStr = configRepository.getModel()
                val provider = configRepository.getProvider()
                // Use a model compatible with the active provider
                if (modelStr.isBlank() || !modelStr.contains(provider.replace("_", ""), ignoreCase = true)) {
                    val allModels = configRepository.fetchAllProviderModels()
                    val compat = allModels.firstOrNull { it.provider == provider }
                    if (compat != null) modelStr = compat.id
                }

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

    fun loadModel() {
        voiceService?.loadModel(object : VoiceRecognitionService.VoiceCallback {
            override fun onPartialResult(text: String) {
                _uiState.update { it.copy(partialText = text) }
            }

            override fun onResult(text: String) {
                // Just store the final text, don't process yet — wait for onFinalResult or stop
                _uiState.update { it.copy(transcript = text, partialText = "") }
            }

            override fun onError(error: String) {
                _uiState.update { it.copy(error = error) }
            }

            override fun onReady() {
                _uiState.update { it.copy(modelLoaded = true) }
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
                        _uiState.update { it.copy(partialText = "", transcript = final) }
                    }
                }
            }

            override fun onFinalResult(hypothesis: String?) {
                hypothesis?.let { text ->
                    val final = extractTextFromJson(text)
                    if (final.isNotBlank()) {
                        _uiState.update { it.copy(
                            partialText = "",
                            transcript = final,
                            isListening = false
                        )}
                        processTranscript(final)
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

    private fun processTranscript(text: String) {
        if (sessionId.isBlank() || isProcessingTranscript || text.isBlank()) return
        isProcessingTranscript = true

        viewModelScope.launch(Dispatchers.IO) {
            _uiState.update { it.copy(isProcessing = true, responseText = "") }

            try {
                _uiState.update { state ->
                    state.copy(messages = state.messages + ChatMessage(role = Role.USER, content = text))
                }

                val response = StringBuilder()
                chatRepository.sendMessageFull(
                    sessionId = sessionId,
                    message = text,
                    model = configRepository.getModel(),
                    onToken = { content, _ ->
                        if (content != null) {
                            response.append(content)
                            _uiState.update { it.copy(responseText = response.toString()) }
                        }
                    },
                    onFinished = {
                        val finalResponse = response.toString()
                        _uiState.update { state ->
                            state.copy(
                                messages = state.messages + ChatMessage(role = Role.ASSISTANT, content = finalResponse),
                                isProcessing = false
                            )
                        }
                        isProcessingTranscript = false
                        speakResponse(finalResponse)
                    }
                )
            } catch (e: Exception) {
                isProcessingTranscript = false
                _uiState.update { it.copy(isProcessing = false, error = "Failed: ${e.message}") }
            }
        }
    }

    private fun speakResponse(text: String) {
        if (!ttsInitialized || text.isBlank()) return
        // Don't stop previous TTS - let it queue naturally
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

    override fun onCleared() {
        super.onCleared()
        voiceService?.destroy()
        tts?.stop()
        tts?.shutdown()
    }
}
