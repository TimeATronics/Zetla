package com.zetla.domain.model

import java.util.UUID

data class Conversation(
    val id: UUID = UUID.randomUUID(),
    val title: String,
    val selectedModel: Model,
    val isStarred: Boolean = false,
    val createdAt: Long = System.currentTimeMillis(),
    val lastUpdatedAt: Long = System.currentTimeMillis(),
    val hasCompactedContext: Boolean = false
)