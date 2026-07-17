package com.zetla.ui.navigation

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.hilt.navigation.compose.hiltViewModel
import androidx.navigation.NavHostController
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.navArgument
import com.zetla.domain.repository.ConfigRepository
import com.zetla.ui.screens.chat.ChatScreen
import com.zetla.ui.screens.settings.SettingsScreen
import com.zetla.ui.screens.settings.SettingsViewModel
import com.zetla.ui.voice.VoiceChatScreen
import com.zetla.ui.voice.VoiceChatViewModel
import java.util.UUID

@Composable
fun ZetlaNavHost(
    navController: NavHostController,
    startDestination: String = Screen.ChatHome.route,
    onThemeChanged: (Boolean) -> Unit = {}
) {
    NavHost(
        navController = navController,
        startDestination = startDestination
    ) {
        composable(Screen.ChatHome.route) {
            LaunchedEffect(Unit) {
                navController.navigate(Screen.Chat.createRoute(UUID.randomUUID().toString())) {
                    popUpTo(Screen.ChatHome.route) { inclusive = true }
                }
            }
        }

        composable(
            route = Screen.Chat.route,
            arguments = listOf(navArgument("sessionId") { type = NavType.StringType })
        ) {
            ChatScreen(
                viewModel = hiltViewModel(),
                onNavigateToSettings = {
                    navController.navigate(Screen.Settings.route)
                },
                onNavigateToVoiceChat = {
                    navController.navigate(Screen.VoiceChat.route)
                }
            )
        }

        composable(Screen.Settings.route) {
            SettingsScreen(
                onBack = { navController.popBackStack() },
                viewModel = hiltViewModel(),
                onThemeChanged = onThemeChanged
            )
        }

        composable(Screen.VoiceChat.route) {
            VoiceChatScreen(
                onBack = { navController.popBackStack() },
                viewModel = hiltViewModel()
            )
        }
    }
}
