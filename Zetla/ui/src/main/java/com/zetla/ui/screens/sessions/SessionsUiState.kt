package com.zetla.ui.screens.sessions

import com.zetla.domain.model.Session

data class SessionsUiState(
    val sessions: List<Session> = emptyList(),
    val isLoading: Boolean = false,
    val error: String? = null,
    val navigateToSessionId: String? = null
) {
    companion object {
        val Default = SessionsUiState()
    }
}

sealed interface SessionsUiEvent {
    data object LoadSessions : SessionsUiEvent
    data class CreateSession(val model: String, val systemPrompt: String) : SessionsUiEvent
    data class DeleteSession(val sessionId: String) : SessionsUiEvent
    data object DismissError : SessionsUiEvent
}
