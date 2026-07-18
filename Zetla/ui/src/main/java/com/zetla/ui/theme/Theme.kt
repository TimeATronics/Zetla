package com.zetla.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable

@Composable
fun ZetlaTheme(
    darkMode: Boolean = true,
    appColorScheme: AppColorScheme = AppColorScheme.DEFAULT,
    content: @Composable () -> Unit
) {
    val pair = appColorSchemes[appColorScheme] ?: appColorSchemes[AppColorScheme.DEFAULT]!!
    val colorScheme = if (darkMode) pair.first else pair.second

    MaterialTheme(
        colorScheme = colorScheme,
        typography = ZetlaTypography,
        content = content
    )
}
