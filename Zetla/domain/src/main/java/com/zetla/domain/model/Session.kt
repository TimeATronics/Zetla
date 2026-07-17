package com.zetla.domain.model

data class Session(
    val id: String,
    val model: String,
    val title: String = "New Chat",
    val createdAt: Long = System.currentTimeMillis()
)
