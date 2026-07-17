package com.zetla.data.mapper

import com.zetla.domain.model.ChatMessage
import com.zetla.domain.model.Conversation
import com.zetla.domain.model.FileAttachment
import com.zetla.domain.model.FileType
import com.zetla.domain.model.Model
import com.zetla.domain.model.Role
import com.zetla.domain.model.Session
import org.json.JSONArray
import org.json.JSONObject

fun String.toChatMessages(): List<ChatMessage> {
    return try {
        val arr = JSONArray(this)
        (0 until arr.length()).mapNotNull { i ->
            val obj = arr.getJSONObject(i)
            ChatMessage(
                content = obj.optString("content", ""),
                role = when (obj.optString("role", "user")) {
                    "assistant" -> Role.ASSISTANT
                    "system" -> Role.SYSTEM
                    else -> Role.USER
                },
                timestamp = obj.optLong("timestamp", System.currentTimeMillis())
            )
        }
    } catch (_: Exception) {
        emptyList()
    }
}

fun String.toSession(): Session? {
    return try {
        val obj = JSONObject(this)
        Session(
            id = obj.optString("session_id", obj.optString("id", "")),
            model = obj.optString("model", ""),
            title = obj.optString("title", "New Chat"),
            createdAt = obj.optLong("created_at", System.currentTimeMillis())
        )
    } catch (_: Exception) {
        null
    }
}

fun Session.toConversation(): Conversation = Conversation(
    id = java.util.UUID.fromString(id),
    selectedModel = Model(id = model, name = model),
    title = title,
    isStarred = false,
    createdAt = createdAt,
    lastUpdatedAt = createdAt
)

fun String.toSessionList(): List<Session> {
    return try {
        val obj = JSONObject(this)
        val arr = obj.optJSONArray("sessions") ?: return emptyList()
        (0 until arr.length()).mapNotNull { i ->
            arr.getJSONObject(i).let { s ->
                Session(
                    id = s.optString("session_id", s.optString("id", "")),
                    model = s.optString("model", ""),
                    title = s.optString("title", "New Chat"),
                    createdAt = s.optLong("created_at", System.currentTimeMillis())
                )
            }
        }
    } catch (_: Exception) {
        emptyList()
    }
}

fun String.toFileAttachments(): List<FileAttachment> {
    return try {
        val obj = JSONObject(this)
        val arr = obj.optJSONArray("files") ?: return emptyList()
        (0 until arr.length()).mapNotNull { i ->
            val f = arr.getJSONObject(i)
            FileAttachment(
                id = f.optString("file_id", f.optString("id", "")),
                name = f.optString("name", ""),
                path = f.optString("path", ""),
                type = f.optString("type", "unknown").toFileType(),
                size = f.optLong("size", 0)
            )
        }
    } catch (_: Exception) {
        emptyList()
    }
}

fun String.toFileType(): FileType = when (lowercase()) {
    "pdf" -> FileType.PDF
    "image", "png", "jpg", "jpeg", "gif", "webp" -> FileType.IMAGE
    "xlsx", "xls", "csv" -> FileType.SPREADSHEET
    "pptx", "ppt" -> FileType.PRESENTATION
    "docx", "doc", "rtf" -> FileType.DOCUMENT
    "txt", "md", "text" -> FileType.TEXT
    else -> FileType.UNKNOWN
}

fun String.toFileAttachment(): FileAttachment? {
    return try {
        val obj = JSONObject(this)
        FileAttachment(
            id = obj.optString("file_id", obj.optString("id", "")),
            name = obj.optString("name", ""),
            path = obj.optString("path", ""),
            type = obj.optString("type", "unknown").toFileType(),
            size = obj.optLong("size", 0)
        )
    } catch (_: Exception) {
        null
    }
}

data class StreamToken(
    val content: String?,
    val reasoning: String?
)

fun parseStreamToken(jsonChunk: String): String? {
    return try {
        val obj = JSONObject(jsonChunk)
        val delta = obj.optString("delta", "")
        if (delta.isNotEmpty()) delta else null
    } catch (_: Exception) {
        jsonChunk
    }
}

fun parseStreamTokenFull(jsonChunk: String): StreamToken? {
    return try {
        val obj = JSONObject(jsonChunk)
        val delta = obj.optString("delta", "")
        val reasoning = obj.optString("reasoning", "")
        if (delta.isNotEmpty() || reasoning.isNotEmpty()) {
            StreamToken(
                content = if (delta.isNotEmpty()) delta else null,
                reasoning = if (reasoning.isNotEmpty()) reasoning else null
            )
        } else null
    } catch (_: Exception) {
        null
    }
}