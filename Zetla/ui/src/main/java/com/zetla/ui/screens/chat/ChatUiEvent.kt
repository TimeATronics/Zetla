package com.zetla.ui.screens.chat

import com.zetla.domain.model.Conversation
import com.zetla.domain.model.ConversationFilter
import com.zetla.domain.model.Model
import com.zetla.domain.model.FileAttachment

sealed interface ChatUiEvent {
    data class OnInputTextEdit(val str: String) : ChatUiEvent
    data object OnMessageSend : ChatUiEvent
    data class OnSetApiKey(val apiKey: String) : ChatUiEvent
    data class OnPreloadMarkdownRequest(val index: Int) : ChatUiEvent
    data object OnCancelPreloadMarkdownJobs : ChatUiEvent
    data object OnFetchModels : ChatUiEvent
    data class OnModelSelected(val model: Model) : ChatUiEvent
    data class OnConversationSelected(val conversation: Conversation) : ChatUiEvent
    data object OnNewChat : ChatUiEvent
    data object OnWebSearchTapped : ChatUiEvent
    data object OnCodingToggled : ChatUiEvent
    data class OnDeleteConversation(val conversation: Conversation) : ChatUiEvent
    data class OnUpdateConversation(val conversation: Conversation) : ChatUiEvent
    data class OnConversationFilterSelected(val filter: ConversationFilter) : ChatUiEvent
    data object OnStopRequest : ChatUiEvent
    data object OnRefreshConfig : ChatUiEvent
    data class OnAttachFile(val file: FileAttachment) : ChatUiEvent
    data class OnRemoveAttachedFile(val fileId: String) : ChatUiEvent
    data object OnClearAttachedFiles : ChatUiEvent
    data object OnDismissOnboarding : ChatUiEvent
    data class OnSetupProvider(val providerId: String, val apiKey: String) : ChatUiEvent
    data class OnReasoningEffortSelected(val effort: String) : ChatUiEvent
}
