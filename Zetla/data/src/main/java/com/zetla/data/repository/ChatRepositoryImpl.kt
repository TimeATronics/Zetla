package com.zetla.data.repository

import android.util.Log
import com.zetla.data.ZetlaCore
import com.zetla.data.StreamCallback
import com.zetla.data.mapper.parseStreamToken
import com.zetla.data.mapper.parseStreamTokenFull
import com.zetla.data.mapper.toChatMessages
import com.zetla.domain.model.ChatMessage
import com.zetla.domain.model.Conversation
import com.zetla.domain.model.Model
import com.zetla.domain.model.ModelCapabilities
import com.zetla.domain.model.Reason
import com.zetla.domain.repository.ChatRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.flow.update
import kotlin.coroutines.resume
import java.util.UUID
import org.json.JSONObject
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class ChatRepositoryImpl @Inject constructor() : ChatRepository {

    private val TAG = "ChatRepoImpl"
    private val conversationsFlow = MutableStateFlow(emptyList<Conversation>())

    private fun nativeId(sessionId: String): String = sessionId.replace("-", "")

    override suspend fun sendMessage(
        sessionId: String,
        message: String,
        model: String,
        onToken: (String) -> Unit,
        onFinished: () -> Unit
    ): Boolean = withContext(Dispatchers.IO) {
        if (model.isNotBlank()) {
            ZetlaCore.nativeSetSessionModel(nativeId(sessionId), model)
        }
        ZetlaCore.nativeSendMessage(nativeId(sessionId), message, object : StreamCallback {
            override fun onToken(jsonChunk: String) {
                parseStreamToken(jsonChunk)?.let { onToken(it) }
            }
            override fun onFinished() {
                onFinished()
            }
        })
    }

    override suspend fun sendMessageFull(
        sessionId: String,
        message: String,
        model: String,
        onToken: (content: String?, reasoning: String?) -> Unit,
        onFinished: () -> Unit
    ): Boolean = withContext(Dispatchers.IO) {
        if (model.isNotBlank()) {
            ZetlaCore.nativeSetSessionModel(nativeId(sessionId), model)
        }
        var finishedCalled = false
        ZetlaCore.nativeSendMessage(nativeId(sessionId), message, object : StreamCallback {
            override fun onToken(jsonChunk: String) {
                val token = parseStreamTokenFull(jsonChunk)
                if (token != null) {
                    onToken(token.content, token.reasoning)
                }
            }
            override fun onFinished() {
                if (finishedCalled) return
                finishedCalled = true
                onFinished()
            }
        })
    }

    override suspend fun sendMessageFullWithFiles(
        sessionId: String,
        message: String,
        model: String,
        fileIds: Array<String>,
        onToken: (content: String?, reasoning: String?) -> Unit,
        onFinished: () -> Unit
    ): Boolean = withContext(Dispatchers.IO) {
        if (model.isNotBlank()) {
            ZetlaCore.nativeSetSessionModel(nativeId(sessionId), model)
        }
        var finishedCalled = false
        ZetlaCore.nativeSendMessageWithFiles(nativeId(sessionId), message, fileIds, object : StreamCallback {
            override fun onToken(jsonChunk: String) {
                val token = parseStreamTokenFull(jsonChunk)
                if (token != null) {
                    onToken(token.content, token.reasoning)
                }
            }
            override fun onFinished() {
                if (finishedCalled) return
                finishedCalled = true
                onFinished()
            }
        })
    }

    override suspend fun sendMessageFullWithImages(
        sessionId: String,
        message: String,
        model: String,
        imageDataUris: Array<String>,
        onToken: (content: String?, reasoning: String?) -> Unit,
        onFinished: () -> Unit
    ): Boolean = withContext(Dispatchers.IO) {
        if (model.isNotBlank()) {
            ZetlaCore.nativeSetSessionModel(nativeId(sessionId), model)
        }
        var finishedCalled = false
        ZetlaCore.nativeSendMessageWithImages(nativeId(sessionId), message, imageDataUris, object : StreamCallback {
            override fun onToken(jsonChunk: String) {
                val token = parseStreamTokenFull(jsonChunk)
                if (token != null) {
                    onToken(token.content, token.reasoning)
                }
            }
            override fun onFinished() {
                if (finishedCalled) return
                finishedCalled = true
                onFinished()
            }
        })
    }

    override suspend fun fetchModels(): List<Model> = withContext(Dispatchers.IO) {
        return@withContext try {
            val json = ZetlaCore.nativeListModels()
            val arr = org.json.JSONArray(json)
            (0 until arr.length()).mapNotNull { i ->
                val obj = arr.getJSONObject(i)
                val id = obj.optString("id", "")
                if (id.isNotEmpty()) {
                    val caps = if (obj.has("capabilities")) {
                        ModelCapabilities.fromJson(obj.getJSONObject("capabilities"))
                    } else ModelCapabilities()
                    Model(
                        id = id,
                        name = obj.optString("name", id),
                        provider = obj.optString("provider", ""),
                        capabilities = caps
                    )
                } else null
            }
        } catch (_: Exception) {
            emptyList()
        }
    }

    override suspend fun loadSession(sessionId: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val nativeSid = nativeId(sessionId)
            Log.d(TAG, "loadSession: $nativeSid")
            val json = ZetlaCore.nativeLoadSession(nativeSid)
            val result = org.json.JSONObject(json).optBoolean("success", false)
            Log.d(TAG, "loadSession result: $result json=$json")
            result
        } catch (e: Exception) {
            Log.e(TAG, "loadSession exception", e)
            false
        }
    }

    override suspend fun getHistory(sessionId: String): List<ChatMessage> = withContext(Dispatchers.IO) {
        try {
            val nativeSid = nativeId(sessionId)
            val json = ZetlaCore.nativeGetHistory(nativeSid)
            val obj = JSONObject(json)
            val success = obj.optBoolean("success", false)
            if (success) {
                val dataStr = obj.optString("data", "{}")
                val messagesStr = JSONObject(dataStr).optString("messages", "[]")
                Log.d(TAG, "getHistory: $nativeSid success=true messages_len=${messagesStr.length}")
                messagesStr.toChatMessages()
            } else {
                Log.w(TAG, "getHistory: $nativeSid success=false json=$json")
                emptyList()
            }
        } catch (e: Exception) {
            Log.e(TAG, "getHistory exception", e)
            emptyList()
        }
    }

    override suspend fun getCompletion(model: Model, message: ChatMessage, apiKey: String?): Result<String?> =
        withContext(Dispatchers.IO) {
            val sessionId = UUID.randomUUID().toString()
            try {
                val createResp = ZetlaCore.nativeCreateSession(model.id, "")
                if (!JSONObject(createResp).optBoolean("success", false)) {
                    return@withContext Result.failure(Exception("Failed to create session"))
                }
            val result = suspendCancellableCoroutine<String?> { cont ->
                ZetlaCore.nativeSendMessage(sessionId, message.content, object : StreamCallback {
                    val response = StringBuilder()
                    override fun onToken(jsonChunk: String) {
                        parseStreamToken(jsonChunk)?.let { response.append(it) }
                    }
                    override fun onFinished() {
                        if (cont.isActive) {
                            cont.resume(if (response.isNotEmpty()) response.toString() else "")
                        }
                    }
                })
            }
                ZetlaCore.nativeDeleteSession(sessionId)
                Result.success(result)
            } catch (e: Exception) {
                Result.failure(e)
            }
        }

    override suspend fun getChatCompletion(model: Model, messages: List<ChatMessage>, apiKey: String?): Result<String?> =
        withContext(Dispatchers.IO) {
            val sessionId = UUID.randomUUID().toString()
            try {
                val createResp = ZetlaCore.nativeCreateSession(model.id, "")
                if (!JSONObject(createResp).optBoolean("success", false)) {
                    return@withContext Result.failure(Exception("Failed to create session"))
                }
                val request = buildMessagesPayload(messages)
                val result = suspendCancellableCoroutine<String?> { cont ->
                ZetlaCore.nativeSendMessage(sessionId, request, object : StreamCallback {
                    val response = StringBuilder()
                    override fun onToken(jsonChunk: String) {
                        parseStreamToken(jsonChunk)?.let { response.append(it) }
                    }
                    override fun onFinished() {
                        if (cont.isActive) {
                            cont.resume(if (response.isNotEmpty()) response.toString() else "")
                        }
                    }
                })
            }
                ZetlaCore.nativeDeleteSession(sessionId)
                Result.success(result)
            } catch (e: Exception) {
                Result.failure(e)
            }
        }

    override suspend fun getChatCompletionFlow(
        model: Model,
        messages: List<ChatMessage>,
        reason: Reason,
        apiKey: String?
    ): Result<Flow<String>> = withContext(Dispatchers.IO) {
        val sessionId = UUID.randomUUID().toString()
        try {
            val createResp = ZetlaCore.nativeCreateSession(model.id, "")
            if (!JSONObject(createResp).optBoolean("success", false)) {
                return@withContext Result.failure(Exception("Failed to create session"))
            }
            val request = buildMessagesPayload(messages)

            val flow = callbackFlow {
                val success = ZetlaCore.nativeSendMessage(sessionId, request, object : StreamCallback {
                    override fun onToken(jsonChunk: String) {
                        parseStreamToken(jsonChunk)?.let { trySend(it) }
                    }
                    override fun onFinished() {
                        ZetlaCore.nativeDeleteSession(sessionId)
                        close()
                    }
                })
                if (!success) {
                    ZetlaCore.nativeDeleteSession(sessionId)
                    close(Exception("Failed to start stream"))
                }
                awaitClose {
                    ZetlaCore.nativeDeleteSession(sessionId)
                }
            }.stateIn(
                kotlinx.coroutines.CoroutineScope(Dispatchers.IO),
                SharingStarted.Lazily,
                ""
            )
            Result.success(flow)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    private fun buildMessagesPayload(messages: List<ChatMessage>): String {
        val body = StringBuilder()
        body.append("{\"messages\":[")
        messages.forEachIndexed { i, msg ->
            if (i > 0) body.append(",")
            body.append("{\"role\":\"")
                .append(msg.role.name.lowercase())
                .append("\",\"content\":\"")
                .append(escapeJson(msg.content))
                .append("\"}")
        }
        body.append("],\"stream\":true}")
        return body.toString()
    }

    private fun escapeJson(s: String): String = s
        .replace("\\", "\\\\")
        .replace("\"", "\\\"")
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")

    override suspend fun getConversations(): List<Conversation> = withContext(Dispatchers.IO) {
        try {
            val json = ZetlaCore.nativeListSessions()
            val obj = JSONObject(json)
            if (obj.optBoolean("success", false)) {
                obj.optString("data", "[]").toConversationList()
            } else {
                emptyList()
            }
        } catch (_: Exception) {
            emptyList()
        }
    }

    override suspend fun refreshConversations() {
        val list = getConversations()
        conversationsFlow.value = list
        Log.d(TAG, "refreshConversations: ${list.size} conversations loaded from disk")
    }

    override fun getConversationsFlow(): Flow<List<Conversation>> = conversationsFlow

    override suspend fun createConversation(title: String, model: Model, isStarred: Boolean, systemPrompt: String): Conversation =
        withContext(Dispatchers.IO) {
            val json = ZetlaCore.nativeCreateSession(model.id, systemPrompt)
            val obj = JSONObject(json)
            if (obj.optBoolean("success", false)) {
                val data = obj.optString("data", "")
                val dataObj = JSONObject(data)
                val conv = Conversation(
                    id = parseUuid(dataObj.optString("session_id", UUID.randomUUID().toString())),
                    selectedModel = model,
                    title = title,
                    isStarred = isStarred,
                    createdAt = dataObj.optLong("created_at", System.currentTimeMillis()),
                    lastUpdatedAt = dataObj.optLong("last_active", System.currentTimeMillis())
                )
                conversationsFlow.update { it + conv }
                Log.d(TAG, "createConversation: id=${conv.id} title='$title'")
                conv
            } else {
                throw Exception(obj.optString("error", "Create session failed"))
            }
        }

    override suspend fun updateConversation(conversation: Conversation) = withContext(Dispatchers.IO) {
        val options = JSONObject().apply {
            put("title", conversation.title)
            put("is_starred", conversation.isStarred)
        }
        ZetlaCore.nativeSetSessionOptions(nativeId(conversation.id.toString()), options.toString())
        conversationsFlow.update { list ->
            list.map { if (it.id == conversation.id) conversation else it }
        }
    }

    override suspend fun deleteConversation(id: UUID) = withContext(Dispatchers.IO) {
        val nativeSid = nativeId(id.toString())
        ZetlaCore.nativeDeleteSession(nativeSid)
        ZetlaCore.nativeDeleteFromStorage(nativeSid)
        Log.d(TAG, "deleteConversation: $id deleted from memory and storage")
        conversationsFlow.update { list -> list.filter { it.id != id } }
    }

    override suspend fun getMessages(conversationId: UUID): List<ChatMessage> = withContext(Dispatchers.IO) {
        try {
            val json = ZetlaCore.nativeGetHistory(nativeId(conversationId.toString()))
            val obj = JSONObject(json)
            if (obj.optBoolean("success", false)) {
                val dataStr = obj.optString("data", "{}")
                JSONObject(dataStr).optString("messages", "[]").toChatMessages()
            } else {
                emptyList()
            }
        } catch (_: Exception) {
            emptyList()
        }
    }

    override suspend fun insertMessage(message: ChatMessage, conversationId: UUID) {
    }

    override suspend fun insertMessages(messages: List<ChatMessage>, conversationId: UUID) {
    }

    override suspend fun setSessionOptions(sessionId: String, optionsJson: String) {
        withContext(Dispatchers.IO) {
            ZetlaCore.nativeSetSessionOptions(nativeId(sessionId), optionsJson)
        }
    }

    override suspend fun setSessionSystemPrompt(sessionId: String, systemPrompt: String) {
        withContext(Dispatchers.IO) {
            ZetlaCore.nativeSetSessionSystemPrompt(nativeId(sessionId), systemPrompt)
        }
    }

    override suspend fun deleteMessages(conversationId: UUID) {
        withContext(Dispatchers.IO) {
            ZetlaCore.nativeClearHistory(nativeId(conversationId.toString()))
        }
    }

    override suspend fun setSessionWebSearch(sessionId: String, enabled: Boolean): Boolean {
        return withContext(Dispatchers.IO) {
            try {
                ZetlaCore.nativeSetSessionWebSearch(nativeId(sessionId), enabled)
            } catch (_: Exception) {
                false
            }
        }
    }

    override suspend fun compactSession(sessionId: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val json = ZetlaCore.nativeCompactSession(nativeId(sessionId))
            JSONObject(json).optBoolean("success", false)
        } catch (_: Exception) { false }
    }

    override suspend fun needsCompaction(sessionId: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val json = ZetlaCore.nativeGetCompactionInfo(nativeId(sessionId))
            val obj = JSONObject(json)
            obj.optBoolean("needs_compaction", false)
        } catch (_: Exception) { false }
    }

    override fun cancelRequest() {
        ZetlaCore.nativeCancelRequest()
    }

    private fun loadConversations() {
        kotlinx.coroutines.runBlocking {
            val list = getConversations()
            conversationsFlow.value = list
        }
    }

    private fun String.toConversationList(): List<Conversation> {
        val array = org.json.JSONArray(this)
        return (0 until array.length()).mapNotNull { i ->
            val obj = array.getJSONObject(i)
            try {
                Conversation(
                    id = parseUuid(obj.optString("session_id", obj.optString("id", ""))),
                    selectedModel = Model(obj.optString("model", ""), obj.optString("model", "")),
                    title = obj.optString("title", "New Chat"),
                    isStarred = obj.optBoolean("is_starred", false),
                    createdAt = obj.optLong("created_at", System.currentTimeMillis()),
                    lastUpdatedAt = obj.optLong("last_active", System.currentTimeMillis()),
                    hasCompactedContext = obj.optBoolean("has_compacted_summary", false)
                )
            } catch (_: Exception) {
                null
            }
        }
    }
}

private fun parseUuid(s: String): UUID {
    val clean = s.replace("-", "")
    if (clean.length != 32) return UUID.randomUUID()
    return UUID.fromString(
        "${clean.substring(0, 8)}-${clean.substring(8, 12)}-${clean.substring(12, 16)}-${clean.substring(16, 20)}-${clean.substring(20)}"
    )
}
