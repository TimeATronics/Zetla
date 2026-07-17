package com.zetla.data

object ZetlaCore {
    init {
        System.loadLibrary("zetla")
    }

    external fun nativeVersion(): String
    external fun nativeInit(): String
    external fun nativeInitWithPath(storagePath: String): String
    external fun nativeShutdown()
    external fun nativeSetApiKey(apiKey: String)
    external fun nativeSetModel(model: String)
    external fun nativeListProviders(): String
    external fun nativeSetProvider(providerId: String): String
    external fun nativeListModels(): String

    external fun nativeCreateSession(model: String, systemPrompt: String): String
    external fun nativeDeleteSession(sessionId: String): String
    external fun nativeGetSessionInfo(sessionId: String): String
    external fun nativeLoadSession(sessionId: String): String
    external fun nativeListSessions(): String
    external fun nativeDeleteFromStorage(sessionId: String): String
    external fun nativeSessionExistsOnDisk(sessionId: String): String

    external fun nativeSendMessage(sessionId: String, message: String, callback: StreamCallback): Boolean
    external fun nativeSendMessageSse(sessionId: String, message: String, callback: SseCallback): Boolean
    external fun nativeSendMessageAgentic(sessionId: String, message: String, callback: AgenticCallback): Boolean

    external fun nativeAddTool(sessionId: String, name: String, description: String, parametersSchema: String): String

    external fun nativeGetHistory(sessionId: String): String
    external fun nativeClearHistory(sessionId: String): String

    external fun nativeSetSessionOptions(sessionId: String, optionsJson: String): String
    external fun nativeGetSessionOptions(sessionId: String): String
    external fun nativeSetSessionModel(sessionId: String, model: String): String

    external fun nativeCompactSession(sessionId: String): String
    external fun nativeGetCompactionInfo(sessionId: String): String

    external fun nativeAddFile(sessionId: String, filePath: String): String
    external fun nativeRemoveFile(sessionId: String, fileId: String): String
    external fun nativeListFiles(sessionId: String): String
    external fun nativeSendMessageWithFiles(sessionId: String, message: String, fileIds: Array<String>, callback: StreamCallback): Boolean

    external fun nativeSetDefaultOptions(optionsJson: String)
    external fun nativeGetDefaultOptions(): String

    external fun nativeSetSystemPrompt(systemPrompt: String)
    external fun nativeGetSystemPrompt(): String

    external fun nativeSetProviderConfig(providerId: String, apiKey: String, baseUrl: String, enabled: Boolean)
    external fun nativeGetProviderConfig(providerId: String): String
    external fun nativeListProviderConfigs(): String
    external fun nativeListProvidersModels(): String

    external fun nativeSetSessionWebSearch(sessionId: String, enabled: Boolean): Boolean
    external fun nativeSetSearchProvider(provider: String)
    external fun nativeSetExaApiKey(apiKey: String)
    external fun nativeSetToolExecutor(sessionId: String, callback: ToolExecutorCallback?)
    external fun nativeCancelRequest()
}

interface StreamCallback {
    fun onToken(jsonChunk: String)
    fun onFinished()
}

interface SseCallback {
    fun onSseData(jsonData: String)
    fun onSseDone()
}

interface AgenticCallback {
    fun onEvent(eventJson: String)
}

interface ToolExecutorCallback {
    fun execute(sessionId: String, toolName: String, argumentsJson: String): String
}
