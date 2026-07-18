package com.zetla.ui.screens.chat.components

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AttachFile
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.ModelTraining
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Web
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.zetla.domain.model.FileAttachment
import com.zetla.domain.model.FileType
import com.zetla.domain.model.Model

@Composable
fun ChatInputTextField(
    modifier: Modifier = Modifier,
    value: String,
    onValueChange: (String) -> Unit,
    onSend: () -> Unit,
    onWebSearch: () -> Unit,
    isWebSearchEnabled: Boolean,
    isCodingEnabled: Boolean = false,
    onCodingToggle: () -> Unit = {},
    isLoadingOrStreamingResponse: Boolean,
    onStopRequest: () -> Unit,
    selectedModel: Model = Model("", ""),
    onModelClick: () -> Unit = {},
    onNavigateToVoiceChat: () -> Unit = {},
    attachedFiles: List<FileAttachment> = emptyList(),
    onAttachFile: () -> Unit = {},
    onRemoveFile: (String) -> Unit = {},
    onShowAttachments: () -> Unit = {},
    onImageClick: (String) -> Unit = {}
) {
    var menuExpanded by remember { mutableStateOf(false) }
    var lineCount by remember { mutableIntStateOf(1) }
    val isMultiLine = lineCount > 1

    Column(
        modifier = modifier.fillMaxWidth()
    ) {
        if (attachedFiles.isNotEmpty()) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState())
                    .padding(horizontal = 12.dp, vertical = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                attachedFiles.forEach { file ->
                    FileChip(
                        file = file,
                        onRemove = { onRemoveFile(file.id) },
                        onClick = if (file.type == FileType.IMAGE) {{ onImageClick(file.path) }} else null
                    )
                }
            }
        }

        if (isWebSearchEnabled) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Surface(
                    shape = RoundedCornerShape(12.dp),
                    color = MaterialTheme.colorScheme.primaryContainer
                ) {
                    Text(
                        text = "[Web Search Active]",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onPrimaryContainer,
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp)
                    )
                }
            }
        }

        if (isCodingEnabled) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Surface(
                    shape = RoundedCornerShape(12.dp),
                    color = MaterialTheme.colorScheme.secondaryContainer
                ) {
                    Text(
                        text = "[Coding Active]",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSecondaryContainer,
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp)
                    )
                }
            }
        }

        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 4.dp, vertical = 4.dp)
        ) {
            OutlinedTextField(
                value = value,
                onValueChange = { newValue ->
                    onValueChange(newValue)
                    lineCount = newValue.lines().size.coerceAtLeast(1)
                },
                modifier = Modifier.fillMaxWidth(),
                placeholder = { Text("Type a message...") },
                maxLines = 8,
                shape = if (isMultiLine) RoundedCornerShape(16.dp) else RoundedCornerShape(24.dp),
                trailingIcon = {
                    if (isLoadingOrStreamingResponse) {
                        IconButton(onClick = onStopRequest) {
                            Icon(
                                imageVector = Icons.Default.Stop,
                                contentDescription = "Stop",
                                tint = MaterialTheme.colorScheme.primary
                            )
                        }
                    }
                },
                colors = OutlinedTextFieldDefaults.colors(
                    focusedBorderColor = MaterialTheme.colorScheme.primary,
                    unfocusedBorderColor = MaterialTheme.colorScheme.surfaceVariant,
                    focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant,
                    unfocusedContainerColor = MaterialTheme.colorScheme.surfaceVariant,
                    cursorColor = MaterialTheme.colorScheme.primary,
                    focusedTextColor = MaterialTheme.colorScheme.onSurface,
                    unfocusedTextColor = MaterialTheme.colorScheme.onSurface
                ),
                enabled = !isLoadingOrStreamingResponse
            )

            // Bottom row with +, mic, and send buttons
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 4.dp, vertical = 2.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Left: "+" button
                IconButton(onClick = { menuExpanded = true }) {
                    Icon(
                        imageVector = Icons.Default.Add,
                        contentDescription = "Options",
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }

                // Right: Mic + Send
                Row(verticalAlignment = Alignment.CenterVertically) {
                    IconButton(onClick = onNavigateToVoiceChat) {
                        Icon(
                            imageVector = Icons.Default.Mic,
                            contentDescription = "Speak",
                            tint = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }

                    if (!isLoadingOrStreamingResponse) {
                        IconButton(
                            onClick = onSend,
                            enabled = value.isNotBlank()
                        ) {
                            Icon(
                                imageVector = Icons.AutoMirrored.Filled.Send,
                                contentDescription = "Send",
                                tint = if (value.isNotBlank()) MaterialTheme.colorScheme.primary
                                else MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }

            DropdownMenu(
                expanded = menuExpanded,
                onDismissRequest = { menuExpanded = false }
            ) {
                DropdownMenuItem(
                    text = { Text("Select Model") },
                    leadingIcon = { Icon(Icons.Default.ModelTraining, contentDescription = null) },
                    onClick = {
                        menuExpanded = false
                        onModelClick()
                    }
                )
                if (attachedFiles.isEmpty()) {
                    DropdownMenuItem(
                        text = { Text("Upload File") },
                        leadingIcon = { Icon(Icons.Default.AttachFile, contentDescription = null) },
                        onClick = {
                            menuExpanded = false
                            onAttachFile()
                        }
                    )
                }
                DropdownMenuItem(
                    text = { Text(if (isWebSearchEnabled) "Disable Web Search" else "Enable Web Search") },
                    leadingIcon = { Icon(Icons.Default.Web, contentDescription = null) },
                    onClick = {
                        menuExpanded = false
                        onWebSearch()
                    }
                )
                DropdownMenuItem(
                    text = { Text(if (isCodingEnabled) "Disable Coding" else "Enable Coding") },
                    leadingIcon = { Icon(Icons.Default.Code, contentDescription = null) },
                    onClick = {
                        menuExpanded = false
                        onCodingToggle()
                    }
                )
            }
        }
    }
}
