package com.zetla

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.TileMode
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.compose.rememberNavController
import com.zetla.domain.repository.ConfigRepository
import com.zetla.ui.navigation.ZetlaNavHost
import com.zetla.ui.theme.AppColorScheme
import com.zetla.ui.theme.ZetlaTheme
import dagger.hilt.android.AndroidEntryPoint
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import javax.inject.Inject

@AndroidEntryPoint
class MainActivity : ComponentActivity() {
    @Inject lateinit var configRepository: ConfigRepository

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        configRepository.init()

        var isDarkMode by mutableStateOf(configRepository.isDarkMode())
        var appColorScheme by mutableStateOf(AppColorScheme.fromId(configRepository.getColorScheme()))

        var showSplashOverlay by mutableStateOf(true)

        setContent {
            Box(modifier = Modifier.fillMaxSize()) {
                ZetlaTheme(darkMode = isDarkMode, appColorScheme = appColorScheme) {
                    Surface(modifier = Modifier.fillMaxSize()) {
                        val navController = rememberNavController()
                        ZetlaNavHost(
                            navController = navController,
                            onThemeChanged = { dark -> isDarkMode = dark },
                            onColorSchemeChanged = { scheme -> appColorScheme = scheme }
                        )
                    }
                }

                if (showSplashOverlay) {
                    val alpha by animateFloatAsState(
                        targetValue = if (showSplashOverlay) 1f else 0f,
                        animationSpec = tween(1200),
                        label = "splash_alpha"
                    )
                    val bgColor = Color(0xFF000000)
                    val highlightColor = Color(0xFF1A1A1A)
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .alpha(alpha)
                            .background(bgColor),
                        contentAlignment = Alignment.Center
                    ) {
                        // Radial gradient highlight effect
                        Canvas(modifier = Modifier.fillMaxSize()) {
                            val centerX = size.width / 2
                            val centerY = size.height / 2
                            val radius = maxOf(size.width, size.height) * 0.6f
                            drawCircle(
                                brush = Brush.radialGradient(
                                    colors = listOf(
                                        highlightColor.copy(alpha = 0.6f),
                                        highlightColor.copy(alpha = 0.2f),
                                        bgColor
                                    ),
                                    center = Offset(centerX, centerY),
                                    radius = radius,
                                    tileMode = TileMode.Clamp
                                ),
                                radius = radius,
                                center = Offset(centerX, centerY)
                            )
                        }
                        Column(
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.Center
                        ) {
                            Icon(
                                painter = painterResource(id = R.drawable.ic_launcher_foreground),
                                contentDescription = "Zetla",
                                modifier = Modifier.size(96.dp),
                                tint = Color.White
                            )
                            Spacer(Modifier.height(16.dp))
                            Text(
                                text = "Zetla",
                                style = MaterialTheme.typography.headlineLarge.copy(
                                    fontWeight = FontWeight.Bold,
                                    color = Color.White,
                                    fontSize = 36.sp
                                )
                            )
                            Spacer(Modifier.height(48.dp))
                            Text(
                                text = "Copyright \u00A9 2026 Aradhya Chakrabarti.",
                                style = MaterialTheme.typography.bodySmall,
                                color = Color.White.copy(alpha = 0.4f),
                                textAlign = TextAlign.Center
                            )
                        }
                    }
                }
            }
        }

        kotlinx.coroutines.MainScope().launch {
            delay(2500)
            showSplashOverlay = false
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        configRepository.shutdown()
    }
}
