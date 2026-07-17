package com.zetla.ui.screens.sessions

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.zetla.domain.model.Session
import com.zetla.domain.repository.SessionRepository
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
open class SessionsViewModel @Inject constructor(
    private val sessionRepository: SessionRepository
) : ViewModel() {

    private val _uiState = MutableStateFlow(SessionsUiState.Default)
    val uiState: StateFlow<SessionsUiState> = _uiState.asStateFlow()

    init {
        onUiEvent(SessionsUiEvent.LoadSessions)
    }

    fun onUiEvent(event: SessionsUiEvent) {
        when (event) {
            is SessionsUiEvent.LoadSessions -> loadSessions()
            is SessionsUiEvent.CreateSession -> createSession(event.model, event.systemPrompt)
            is SessionsUiEvent.DeleteSession -> deleteSession(event.sessionId)
            is SessionsUiEvent.DismissError -> _uiState.update { it.copy(error = null) }
        }
    }

    private fun loadSessions() {
        _uiState.update { it.copy(isLoading = true) }
        viewModelScope.launch(Dispatchers.IO) {
            val result = sessionRepository.listSessions()
            _uiState.update {
                it.copy(
                    sessions = result.data ?: emptyList(),
                    isLoading = false,
                    error = result.error
                )
            }
        }
    }

    private fun createSession(model: String, systemPrompt: String) {
        viewModelScope.launch(Dispatchers.IO) {
            val result = sessionRepository.createSession(model, systemPrompt)
            if (result.success) {
                _uiState.update {
                    it.copy(
                        sessions = listOf(result.data!!) + it.sessions,
                        navigateToSessionId = result.data?.id
                    )
                }
            } else {
                _uiState.update { it.copy(error = result.error) }
            }
        }
    }

    private fun deleteSession(sessionId: String) {
        viewModelScope.launch(Dispatchers.IO) {
            sessionRepository.deleteSession(sessionId)
            loadSessions()
        }
    }
}
