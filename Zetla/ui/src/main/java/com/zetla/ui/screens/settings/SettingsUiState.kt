package com.zetla.ui.screens.settings

import android.speech.tts.Voice
import com.zetla.domain.repository.ProviderConfig
import com.zetla.domain.repository.ProviderInfo
import com.zetla.ui.theme.AppColorScheme

data class SettingsUiState(
    val providers: List<ProviderInfo> = emptyList(),
    val isDarkMode: Boolean = true,
    val colorScheme: AppColorScheme = AppColorScheme.DEFAULT,
    val version: String = "",
    val savedMessage: String? = null,
    val error: String? = null,
    val systemPrompt: String = "",
    val systemPromptDraft: String = "",
    val providerConfigs: List<ProviderConfig> = emptyList(),
    val isRefetchingModels: Boolean = false,
    val ttsVoices: List<Voice> = emptyList(),
    val selectedTtsVoiceName: String = ""
) {
    companion object {
        val Default = SettingsUiState()
    }
}

sealed interface SettingsUiEvent {
    data object ToggleDarkMode : SettingsUiEvent
    data object LoadSettings : SettingsUiEvent
    data object SaveAllSettings : SettingsUiEvent
    data object DismissError : SettingsUiEvent
    data object DismissSavedMessage : SettingsUiEvent
    data class SetSystemPromptDraft(val prompt: String) : SettingsUiEvent
    data object SaveSystemPrompt : SettingsUiEvent
    data object RestoreDefaultSystemPrompt : SettingsUiEvent
    data class SaveProviderConfigDirect(
        val providerId: String,
        val apiKey: String,
        val baseUrl: String,
        val enabled: Boolean
    ) : SettingsUiEvent
    data object RefetchModels : SettingsUiEvent
    data class SelectColorScheme(val scheme: AppColorScheme) : SettingsUiEvent
    data class SelectTtsVoice(val voiceName: String) : SettingsUiEvent
    data class SetTtsVoices(val voices: List<Voice>) : SettingsUiEvent
}
