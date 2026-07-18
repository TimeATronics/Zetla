package com.zetla.ui.screens.chat

import androidx.compose.runtime.Immutable
import com.zetla.domain.model.Conversation
import com.zetla.domain.model.ConversationFilter
import com.zetla.domain.model.FileAttachment
import com.zetla.domain.model.Model
import java.util.UUID

@Immutable
data class ChatUiState(
    val models: List<Model>,
    val selectedModel: Model,
    val selectedConversation: Conversation?,
    val streamingResponse: String?,
    val streamingThinking: String?,
    val isLoadingResponse: Boolean,
    val isStreamingResponse: Boolean,
    val isFetchingModels: Boolean,
    val apiKey: String?,
    val isProviderConfigured: Boolean = false,
    val isInitialized: Boolean = false,
    val showOnboarding: Boolean = false,
    val availableProviders: List<com.zetla.domain.repository.ProviderInfo> = emptyList(),
    val inputText: String,
    val isWebSearchEnabled: Boolean,
    val isCodingEnabled: Boolean = false,
    val messages: List<UiMessage>,
    val conversations: List<Conversation>,
    val conversationFilter: ConversationFilter,
    val attachedFiles: List<FileAttachment> = emptyList(),
    val isExtractingPdf: Boolean = false,
    val error: String? = null,
    val modelsByProvider: Map<String, List<Model>> = emptyMap(),
    val reasoningEffort: String = ""
)

@Immutable
data class UiMessage(
    val id: String,
    val content: String?,
    val isUser: Boolean,
    val thinkingText: String? = null
)

val DefaultChatUiState = ChatUiState(
    models = emptyList(),
    selectedModel = Model.defaultModel,
    selectedConversation = null,
    streamingResponse = null,
    streamingThinking = null,
    isLoadingResponse = false,
    isStreamingResponse = false,
    isFetchingModels = false,
    apiKey = null,
    isInitialized = false,
    inputText = "",
    isWebSearchEnabled = false,
    isCodingEnabled = false,
    messages = emptyList(),
    conversations = emptyList(),
    conversationFilter = ConversationFilter.RECENT,
    error = null
)
