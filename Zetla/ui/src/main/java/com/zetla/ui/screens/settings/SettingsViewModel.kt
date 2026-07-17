package com.zetla.ui.screens.settings

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.zetla.domain.repository.ConfigRepository
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class SettingsViewModel @Inject constructor(
    private val configRepository: ConfigRepository
) : ViewModel() {

    private val _uiState = MutableStateFlow(SettingsUiState.Default)
    val uiState: StateFlow<SettingsUiState> = _uiState.asStateFlow()

    init {
        loadSettings()
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
        }
    }

    private fun loadSettings() {
        val providers = configRepository.listProviders()
        val systemPrompt = configRepository.getSystemPrompt()
        val providerConfigs = configRepository.listProviderConfigs()
        _uiState.update {
            it.copy(
                providers = providers,
                isDarkMode = configRepository.isDarkMode(),
                version = configRepository.getVersion(),
                systemPrompt = systemPrompt,
                systemPromptDraft = systemPrompt,
                providerConfigs = providerConfigs
            )
        }
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
