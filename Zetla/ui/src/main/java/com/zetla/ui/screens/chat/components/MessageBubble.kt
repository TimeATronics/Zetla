package com.zetla.ui.screens.chat.components

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.ContentCopy
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.unit.dp
import androidx.compose.ui.graphics.Color
import com.zetla.domain.model.ChatMessage
import com.zetla.domain.model.Role
import com.zetla.ui.theme.UserBubble
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun MessageBubble(
    message: ChatMessage,
    modifier: Modifier = Modifier
) {
    val isUser = message.role == Role.USER
    val bubbleColor = if (isUser) UserBubble else MaterialTheme.colorScheme.surfaceVariant
    val alignment: Alignment = if (isUser) Alignment.CenterEnd else Alignment.CenterStart
    val textColor = if (isUser) Color.White else MaterialTheme.colorScheme.onSurface

    var showCopyFeedback by remember { mutableStateOf(false) }
    val clipboardManager = LocalClipboardManager.current
    val scope = rememberCoroutineScope()

    Column(
        modifier = modifier.fillMaxWidth(),
        horizontalAlignment = if (isUser) Alignment.End else Alignment.Start
    ) {
        Box(
            modifier = Modifier.fillMaxWidth(),
            contentAlignment = alignment
        ) {
            Surface(
                modifier = Modifier
                    .widthIn(max = 520.dp)
                    .padding(
                        start = if (isUser) 48.dp else 12.dp,
                        end = if (isUser) 12.dp else 48.dp,
                        top = 2.dp,
                        bottom = 2.dp
                    ),
                shape = RoundedCornerShape(
                    topStart = 16.dp,
                    topEnd = 16.dp,
                    bottomStart = if (isUser) 16.dp else 4.dp,
                    bottomEnd = if (isUser) 4.dp else 16.dp
                ),
                color = bubbleColor,
                tonalElevation = 2.dp
            ) {
                MarkdownText(
                    markdown = message.content.ifEmpty { "..." },
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
                    color = textColor
                )
            }
        }

        Row(
            modifier = Modifier
                .padding(
                    start = if (isUser) 0.dp else 12.dp,
                    end = if (isUser) 12.dp else 0.dp,
                    top = 2.dp
                ),
            verticalAlignment = Alignment.CenterVertically
        ) {
            if (!isUser) {
                IconButton(
                    onClick = {
                        clipboardManager.setText(AnnotatedString(message.content))
                        showCopyFeedback = true
                        scope.launch {
                            delay(1500)
                            showCopyFeedback = false
                        }
                    },
                    modifier = Modifier.size(24.dp)
                ) {
                    Icon(
                        imageVector = Icons.Outlined.ContentCopy,
                        contentDescription = "Copy",
                        tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f),
                        modifier = Modifier.size(14.dp)
                    )
                }
                if (showCopyFeedback) {
                    Text(
                        text = "Copied",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f),
                        modifier = Modifier.padding(start = 2.dp)
                    )
                }
            }
        }
    }
}
