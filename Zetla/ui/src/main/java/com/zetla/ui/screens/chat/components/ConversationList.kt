package com.zetla.ui.screens.chat.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.draw.clip
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.zetla.domain.model.Conversation
import com.zetla.domain.model.ConversationFilter

@Composable
fun ConversationList(
    conversations: List<Conversation>,
    modifier: Modifier = Modifier,
    selectedConversation: Conversation?,
    onConversationSelected: (Conversation) -> Unit,
    conversationFilter: ConversationFilter,
    onConversationFilterSelected: (ConversationFilter) -> Unit
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(vertical = 16.dp)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            ConversationFilter.entries.forEach { filter ->
                val isSelected = conversationFilter == filter
                Text(
                    text = filter.label,
                    modifier = Modifier
                        .weight(1f)
                        .padding(vertical = 8.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .then(
                            if (isSelected) Modifier.background(
                                MaterialTheme.colorScheme.primary,
                                RoundedCornerShape(8.dp)
                            ) else Modifier
                        )
                        .clickable { onConversationFilterSelected(filter) }
                        .padding(vertical = 8.dp),
                    color = if (isSelected) {
                        MaterialTheme.colorScheme.onPrimary
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    },
                    style = MaterialTheme.typography.bodySmall,
                    textAlign = TextAlign.Center,
                    maxLines = 1
                )
            }
        }

        val displayConversations = when (conversationFilter) {
            ConversationFilter.RECENT -> conversations
            ConversationFilter.STARRED -> conversations.filter { it.isStarred }
        }

        if (displayConversations.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(16.dp),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = "No conversations yet",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodyMedium
                )
            }
        } else {
            LazyColumn(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(top = 8.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                items(
                    items = displayConversations,
                    key = { it.id.toString() }
                ) { conversation ->
                    ConversationItem(
                        conversation = conversation,
                        isSelected = selectedConversation?.id == conversation.id,
                        onClick = { onConversationSelected(conversation) }
                    )
                }
            }
        }
    }
}

@Composable
private fun ConversationItem(
    conversation: Conversation,
    isSelected: Boolean,
    onClick: () -> Unit
) {
    val backgroundColor = if (isSelected) {
        MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
    } else {
        MaterialTheme.colorScheme.surface
    }

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 4.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(backgroundColor)
            .clickable(onClick = onClick)
            .padding(12.dp)
    ) {
        Column {
            Row(verticalAlignment = Alignment.CenterVertically) {
                if (conversation.isSpace) {
                    Icon(
                        imageVector = Icons.Filled.Search,
                        contentDescription = "Space",
                        modifier = Modifier.padding(end = 4.dp),
                        tint = MaterialTheme.colorScheme.primary
                    )
                }
                Text(
                    text = conversation.title,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodyMedium,
                    color = if (isSelected) {
                        MaterialTheme.colorScheme.primary
                    } else {
                        MaterialTheme.colorScheme.onSurface
                    }
                )
            }
            Text(
                text = if (conversation.selectedModel.provider.isNotEmpty())
                    "${conversation.selectedModel.provider.replaceFirstChar { it.uppercase() }} / ${conversation.selectedModel.id}"
                else conversation.selectedModel.id,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}
