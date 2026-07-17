package com.zetla.ui.navigation

sealed class Screen(val route: String) {
    data object ChatHome : Screen("chat_home")
    data object Chat : Screen("chat/{sessionId}") {
        fun createRoute(sessionId: String) = "chat/$sessionId"
    }
    data object Settings : Screen("settings")
    data object VoiceChat : Screen("voice_chat")
}
