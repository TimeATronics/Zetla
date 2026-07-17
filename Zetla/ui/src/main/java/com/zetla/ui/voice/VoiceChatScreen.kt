package com.zetla.ui.voice

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.zetla.domain.model.Role
import kotlin.math.sin

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun VoiceChatScreen(
    onBack: () -> Unit,
    viewModel: VoiceChatViewModel
) {
    val uiState by viewModel.uiState.collectAsStateWithLifecycle()
    val snackbarHostState = remember { SnackbarHostState() }
    val context = LocalContext.current

    val hasPermission = ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) ==
        PackageManager.PERMISSION_GRANTED

    val permissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission()
    ) { isGranted ->
        if (isGranted) viewModel.loadModel()
    }

    LaunchedEffect(Unit) {
        if (hasPermission) viewModel.loadModel()
        else permissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
    }

    LaunchedEffect(uiState.error) {
        uiState.error?.let {
            snackbarHostState.showSnackbar(it)
            viewModel.clearError()
        }
    }

    // Horizontal waveform animation
    val infiniteTransition = rememberInfiniteTransition(label = "waveform")
    val waveformOffset by infiniteTransition.animateFloat(
        initialValue = 0f,
        targetValue = 200f,
        animationSpec = infiniteRepeatable(
            animation = tween(1200, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "waveform_offset"
    )

    Scaffold(
        modifier = Modifier.fillMaxSize(),
        containerColor = MaterialTheme.colorScheme.background,
        topBar = {
            TopAppBar(
                title = { Text("Voice") },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface,
                    titleContentColor = MaterialTheme.colorScheme.onSurface
                ),
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back", tint = MaterialTheme.colorScheme.onSurface)
                    }
                }
            )
        },
        snackbarHost = { SnackbarHost(snackbarHostState) }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Spacer(Modifier.weight(0.15f))

            // Mic button circle
            Box(
                modifier = Modifier
                    .size(if (uiState.isListening || uiState.isSpeaking) 100.dp else 72.dp)
                    .clip(CircleShape)
                    .background(
                        when {
                            uiState.isSpeaking -> MaterialTheme.colorScheme.tertiary
                            uiState.isListening -> MaterialTheme.colorScheme.error
                            else -> MaterialTheme.colorScheme.primary
                        }
                    ),
                contentAlignment = Alignment.Center
            ) {
                IconButton(
                    onClick = {
                        if (uiState.isListening) viewModel.stopListening()
                        else {
                            viewModel.interruptSpeech()
                            viewModel.startListening()
                        }
                    },
                    modifier = Modifier.size(56.dp),
                    enabled = uiState.modelLoaded
                ) {
                    Icon(
                        imageVector = Icons.Default.Mic,
                        contentDescription = if (uiState.isListening) "Stop" else "Start",
                        tint = Color.White,
                        modifier = Modifier.size(32.dp)
                    )
                }
            }

            // Horizontal waveform line below mic
            if (uiState.isSpeaking) {
                Spacer(Modifier.height(16.dp))
                val accentColor = MaterialTheme.colorScheme.tertiary
                Canvas(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 40.dp)
                        .height(32.dp)
                ) {
                    val w = size.width
                    val h = size.height
                    val barCount = 24
                    val barWidth = w / (barCount * 2)
                    for (i in 0 until barCount) {
                        val phase = i.toFloat() * 0.5f + waveformOffset * 0.05f
                        val barHeight = (sin(phase.toDouble()).toFloat() + 1f) * h * 0.35f + 4f
                        val x = i * barWidth * 2 + barWidth * 0.5f
                        drawRoundRect(
                            color = accentColor.copy(alpha = 0.7f),
                            topLeft = androidx.compose.ui.geometry.Offset(x, h / 2 - barHeight / 2),
                            size = androidx.compose.ui.geometry.Size(barWidth * 0.7f, barHeight),
                            cornerRadius = androidx.compose.ui.geometry.CornerRadius(barWidth * 0.35f)
                        )
                    }
                }
            }

            Spacer(Modifier.height(16.dp))

            // Status text below waveform
            Text(
                text = when {
                    !uiState.modelLoaded -> "Loading..."
                    uiState.isSpeaking -> "Speaking"
                    uiState.isListening && uiState.partialText.isNotBlank() -> uiState.partialText
                    uiState.isListening -> "Listening..."
                    uiState.isProcessing -> "Thinking..."
                    else -> ""
                },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier.padding(horizontal = 32.dp)
            )

            Spacer(Modifier.height(16.dp))

            // Scrollable conversation text area
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .padding(horizontal = 32.dp),
                shape = RoundedCornerShape(16.dp),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f)
            ) {
                val scrollState = rememberScrollState()
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(16.dp)
                        .verticalScroll(scrollState)
                ) {
                    // Show message history from state
                    uiState.messages.forEach { msg ->
                        val prefix = if (msg.role == Role.USER) "You" else "AI"
                        Text(
                            text = "$prefix: ${msg.content}",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurface,
                            modifier = Modifier.padding(bottom = 8.dp)
                        )
                    }
                    // Current transcript from this turn
                    if (uiState.transcript.isNotBlank() && uiState.messages.none { it.content == uiState.transcript && it.role == Role.USER }) {
                        Text(
                            text = "You: ${uiState.transcript}",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurface,
                            modifier = Modifier.padding(bottom = 8.dp)
                        )
                    }
                    // Streaming response
                    if (uiState.responseText.isNotBlank() && uiState.messages.none { it.content == uiState.responseText && it.role == Role.ASSISTANT }) {
                        Text(
                            text = "AI: ${uiState.responseText}",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                    // Fallback empty state
                    if (uiState.messages.isEmpty() && uiState.transcript.isBlank() && uiState.responseText.isBlank()) {
                        Text(
                            text = "Tap the microphone to start",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f),
                            textAlign = TextAlign.Center,
                            modifier = Modifier.fillMaxWidth()
                        )
                    }
                }
            }

            Spacer(Modifier.height(48.dp))
        }
    }
}
