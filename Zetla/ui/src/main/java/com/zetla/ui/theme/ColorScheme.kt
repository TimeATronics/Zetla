package com.zetla.ui.theme

import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color

enum class AppColorScheme(val id: String, val displayName: String) {
    DEFAULT("default", "Default"),
    GRUVBOX("gruvbox", "Gruvbox"),
    SOLARIZED("solarized", "Solarized"),
    NORD("nord", "Nord"),
    CATPPUCCIN("catppuccin", "Catppuccin Mocha");

    companion object {
        fun fromId(id: String): AppColorScheme = entries.find { it.id == id } ?: DEFAULT
    }
}

private val DefaultDark = darkColorScheme(
    background = Color(0xFF0F1117),
    surface = Color(0xFF1A1D2E),
    surfaceVariant = Color(0xFF242840),
    primary = Color(0xFF6C63FF),
    primaryContainer = Color(0xFF5A52E0),
    onPrimary = Color.White,
    secondary = Color(0xFF03DAC6),
    onBackground = Color(0xFFE0E0E0),
    onSurface = Color(0xFFE0E0E0),
    onSurfaceVariant = Color(0xFF9E9E9E),
    error = Color(0xFFCF6679),
    outline = Color(0xFF2A2D42)
)

private val DefaultLight = lightColorScheme(
    background = Color(0xFFF5F5F5),
    surface = Color.White,
    surfaceVariant = Color(0xFFE8E8E8),
    primary = Color(0xFF6C63FF),
    primaryContainer = Color(0xFFE8E0FF),
    onPrimary = Color.White,
    secondary = Color(0xFF03DAC6),
    onBackground = Color(0xFF1C1B1F),
    onSurface = Color(0xFF1C1B1F),
    onSurfaceVariant = Color(0xFF49454F),
    error = Color(0xFFCF6679),
    outline = Color(0xFFCAC4D0)
)

private val GruvboxDark = darkColorScheme(
    background = Color(0xFF282828),
    surface = Color(0xFF3C3836),
    surfaceVariant = Color(0xFF504945),
    primary = Color(0xFFD79921),
    primaryContainer = Color(0xFFB57614),
    onPrimary = Color(0xFF1D2021),
    secondary = Color(0xFF8EC07C),
    onBackground = Color(0xFFEBDDB2),
    onSurface = Color(0xFFEBDDB2),
    onSurfaceVariant = Color(0xFFA89984),
    error = Color(0xFFCC241D),
    outline = Color(0xFF665C54)
)

private val GruvboxLight = lightColorScheme(
    background = Color(0xFFFBF1C7),
    surface = Color(0xFFEBDDB2),
    surfaceVariant = Color(0xFFD5C4A1),
    primary = Color(0xFFB57614),
    primaryContainer = Color(0xFFD79921),
    onPrimary = Color.White,
    secondary = Color(0xFF79740E),
    onBackground = Color(0xFF3C3836),
    onSurface = Color(0xFF3C3836),
    onSurfaceVariant = Color(0xFF7C6F64),
    error = Color(0xFF9D0006),
    outline = Color(0xFFBDAA93)
)

private val SolarizedDark = darkColorScheme(
    background = Color(0xFF002B36),
    surface = Color(0xFF073642),
    surfaceVariant = Color(0xFF586E75),
    primary = Color(0xFF268BD2),
    primaryContainer = Color(0xFF6C71C4),
    onPrimary = Color.White,
    secondary = Color(0xFF859900),
    onBackground = Color(0xFF93A1A1),
    onSurface = Color(0xFF93A1A1),
    onSurfaceVariant = Color(0xFF657B83),
    error = Color(0xFFDC322F),
    outline = Color(0xFF475B62)
)

private val SolarizedLight = lightColorScheme(
    background = Color(0xFFFDF6E3),
    surface = Color(0xFFEEE8D5),
    surfaceVariant = Color(0xFF93A1A1),
    primary = Color(0xFF268BD2),
    primaryContainer = Color(0xFF6C71C4),
    onPrimary = Color.White,
    secondary = Color(0xFF859900),
    onBackground = Color(0xFF586E75),
    onSurface = Color(0xFF586E75),
    onSurfaceVariant = Color(0xFF839496),
    error = Color(0xFFDC322F),
    outline = Color(0xFFB3B3A2)
)

private val NordDark = darkColorScheme(
    background = Color(0xFF2E3440),
    surface = Color(0xFF3B4252),
    surfaceVariant = Color(0xFF434C5E),
    primary = Color(0xFF88C0D0),
    primaryContainer = Color(0xFF81A1C1),
    onPrimary = Color(0xFF2E3440),
    secondary = Color(0xFFA3BE8C),
    onBackground = Color(0xFFD8DEE9),
    onSurface = Color(0xFFD8DEE9),
    onSurfaceVariant = Color(0xFF81A1C1),
    error = Color(0xFFBF616A),
    outline = Color(0xFF4C566A)
)

private val CatppuccinDark = darkColorScheme(
    background = Color(0xFF11111B),
    surface = Color(0xFF1E1E2E),
    surfaceVariant = Color(0xFF313244),
    primary = Color(0xFFCBA6F7),
    primaryContainer = Color(0xFFB4BEFE),
    onPrimary = Color(0xFF11111B),
    secondary = Color(0xFFA6E3A1),
    onBackground = Color(0xFFCDD6F4),
    onSurface = Color(0xFFCDD6F4),
    onSurfaceVariant = Color(0xFFA6ADC8),
    error = Color(0xFFF38BA8),
    outline = Color(0xFF45475A)
)

val appColorSchemes: Map<AppColorScheme, Pair<androidx.compose.material3.ColorScheme, androidx.compose.material3.ColorScheme>> = mapOf(
    AppColorScheme.DEFAULT to (DefaultDark to DefaultLight),
    AppColorScheme.GRUVBOX to (GruvboxDark to GruvboxLight),
    AppColorScheme.SOLARIZED to (SolarizedDark to SolarizedLight),
    AppColorScheme.NORD to (NordDark to DefaultLight),
    AppColorScheme.CATPPUCCIN to (CatppuccinDark to DefaultLight)
)
