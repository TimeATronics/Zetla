package com.zetla.domain.repository

import com.zetla.domain.model.ChatMessage
import com.zetla.domain.model.Conversation
import com.zetla.domain.model.Model
import com.zetla.domain.model.Reason
import com.zetla.domain.model.ZetlaResult

interface ChatRepository {
    suspend fun sendMessage(sessionId: String, message: String, model: String, onToken: (String) -> Unit, onFinished: () -> Unit): Boolean
    suspend fun sendMessageFull(sessionId: String, message: String, model: String, onToken: (content: String?, reasoning: String?) -> Unit, onFinished: () -> Unit): Boolean
    suspend fun sendMessageFullWithFiles(sessionId: String, message: String, model: String, fileIds: Array<String>, onToken: (content: String?, reasoning: String?) -> Unit, onFinished: () -> Unit): Boolean
    suspend fun getHistory(sessionId: String): List<ChatMessage>
    suspend fun loadSession(sessionId: String): Boolean
    suspend fun fetchModels(): List<Model>

    suspend fun getCompletion(model: Model, message: ChatMessage, apiKey: String? = null): Result<String?>
    suspend fun getChatCompletion(model: Model, messages: List<ChatMessage>, apiKey: String? = null): Result<String?>
    suspend fun getChatCompletionFlow(model: Model, messages: List<ChatMessage>, reason: Reason = Reason.None, apiKey: String? = null): Result<kotlinx.coroutines.flow.Flow<String>>

    suspend fun getConversations(): List<Conversation>
    suspend fun refreshConversations()
    fun getConversationsFlow(): kotlinx.coroutines.flow.Flow<List<Conversation>>

    suspend fun createConversation(title: String, model: Model = Model.defaultModel, isStarred: Boolean = false, systemPrompt: String = ""): Conversation
    suspend fun updateConversation(conversation: Conversation)
    suspend fun deleteConversation(id: java.util.UUID)
    suspend fun getMessages(conversationId: java.util.UUID): List<ChatMessage>
    suspend fun insertMessage(message: ChatMessage, conversationId: java.util.UUID)
    suspend fun insertMessages(messages: List<ChatMessage>, conversationId: java.util.UUID)
    suspend fun deleteMessages(conversationId: java.util.UUID)
    suspend fun setSessionOptions(sessionId: String, optionsJson: String)
    suspend fun setSessionWebSearch(sessionId: String, enabled: Boolean): Boolean
    suspend fun compactSession(sessionId: String): Boolean
    suspend fun needsCompaction(sessionId: String): Boolean
    fun cancelRequest()
}
