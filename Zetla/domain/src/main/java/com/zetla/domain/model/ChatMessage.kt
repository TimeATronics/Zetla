package com.zetla.domain.model

data class ChatMessage(
    val content: String,
    val role: Role,
    val timestamp: Long = System.currentTimeMillis()
)
