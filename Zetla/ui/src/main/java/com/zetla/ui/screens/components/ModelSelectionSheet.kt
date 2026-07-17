package com.zetla.ui.screens.components

import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.zetla.domain.model.Model
import com.zetla.ui.screens.chat.components.DotsTyping

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ModelSelectionSheet(
    isVisible: Boolean,
    models: List<Model>,
    selectedModel: Model,
    onModelSelected: (Model) -> Unit,
    isFetchingModels: Boolean,
    fetchModels: () -> Unit,
    onDismiss: () -> Unit,
    modelsByProvider: Map<String, List<Model>> = emptyMap(),
    reasoningEffort: String = "",
    onReasoningEffortSelected: (String) -> Unit = {}
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    LaunchedEffect(isVisible) {
        if (isVisible) sheetState.show()
        else sheetState.hide()
    }

    if (isVisible) {
        ModalBottomSheet(
            onDismissRequest = onDismiss,
            sheetState = sheetState,
            shape = RoundedCornerShape(topStart = 16.dp, topEnd = 16.dp),
            containerColor = MaterialTheme.colorScheme.surface
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .statusBarsPadding()
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Spacer(Modifier.height(8.dp))

                    Text(
                        text = "Select Model",
                        style = MaterialTheme.typography.headlineSmall,
                        color = MaterialTheme.colorScheme.onSurface
                    )

                    Spacer(Modifier.height(16.dp))

                    if (models.isNotEmpty()) {
                        LazyColumn(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 16.dp),
                            contentPadding = PaddingValues(bottom = 16.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            if (modelsByProvider.isNotEmpty()) {
                                val providers = modelsByProvider.keys.toList().sorted()
                                providers.forEach { provider ->
                                    item(key = "header_$provider") {
                                        Text(
                                            text = provider.replaceFirstChar { it.uppercase() },
                                            style = MaterialTheme.typography.titleSmall,
                                            color = MaterialTheme.colorScheme.primary,
                                            modifier = Modifier.padding(top = 8.dp, bottom = 4.dp)
                                        )
                                    }
                                    items(items = modelsByProvider[provider] ?: emptyList(), key = { "${it.provider}/${it.id}" }) { model ->
                                        ModelItem(
                                            model = model,
                                            isSelected = selectedModel.id == model.id && selectedModel.provider == model.provider,
                                            onClick = { onModelSelected(model) }
                                        )
                                    }
                                }
                            } else {
                                items(items = models, key = { "${it.provider}/${it.id}" }) { model ->
                                    ModelItem(
                                        model = model,
                                        isSelected = selectedModel.id == model.id && selectedModel.provider == model.provider,
                                        onClick = { onModelSelected(model) }
                                    )
                                }
                            }
                        }
                    } else if (isFetchingModels) {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(120.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            DotsTyping()
                        }
                    }

                    if (selectedModel.capabilities.thinkingLevels.isNotEmpty()) {
                        Spacer(Modifier.height(16.dp))
                        Text(
                            text = "Reasoning Level",
                            style = MaterialTheme.typography.titleSmall,
                            color = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.padding(horizontal = 16.dp)
                        )
                        Spacer(Modifier.height(8.dp))
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .horizontalScroll(rememberScrollState())
                                .padding(horizontal = 16.dp),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            selectedModel.capabilities.thinkingLevels.forEach { level ->
                                FilterChip(
                                    selected = reasoningEffort == level || (reasoningEffort.isEmpty() && level == selectedModel.capabilities.thinkingLevels.first()),
                                    onClick = { onReasoningEffortSelected(level) },
                                    label = {
                                        Text(
                                            level.replaceFirstChar { it.uppercase() },
                                            fontSize = 12.sp
                                        )
                                    },
                                    colors = FilterChipDefaults.filterChipColors(
                                        selectedContainerColor = MaterialTheme.colorScheme.primaryContainer,
                                        selectedLabelColor = MaterialTheme.colorScheme.onPrimaryContainer
                                    )
                                )
                            }
                        }
                    }

                    Spacer(Modifier.height(24.dp))

                    Button(
                        onClick = { fetchModels() },
                        enabled = !isFetchingModels
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                            Spacer(Modifier.width(8.dp))
                            Text("Refresh Models")
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun ModelItem(
    model: Model,
    isSelected: Boolean,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
        shape = RoundedCornerShape(12.dp),
        color = if (isSelected) {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.1f)
        } else {
            MaterialTheme.colorScheme.surface
        },
        tonalElevation = if (isSelected) 2.dp else 0.dp
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column {
                Text(
                    text = model.id,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Row {
                    if (model.provider.isNotEmpty()) {
                        Text(
                            text = model.provider.replaceFirstChar { it.uppercase() },
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.primary
                        )
                        if (model.capabilities.contextWindow > 0) {
                            Text(
                                text = " | ",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                    if (model.capabilities.contextWindow > 0) {
                        Text(
                            text = "ctx: ${model.capabilities.contextWindow} | max_out: ${model.capabilities.maxOutputTokens}" +
                                if (model.capabilities.supportsTools) " | tools" else "",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                if (model.name != model.id) {
                    Text(
                        text = model.name,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            if (isSelected) {
                Icon(
                    imageVector = Icons.Default.Check,
                    contentDescription = "Selected",
                    tint = MaterialTheme.colorScheme.primary
                )
            }
        }
    }
}
