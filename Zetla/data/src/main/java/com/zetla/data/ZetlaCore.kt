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
    external fun nativeCreateSpace(model: String, systemPrompt: String): String
    external fun nativeDeleteSession(sessionId: String): String
    external fun nativeIsSpace(sessionId: String): String
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
    external fun nativeSendMessageWithImages(sessionId: String, message: String, imageDataUris: Array<String>, callback: StreamCallback): Boolean

    external fun nativeSetDefaultOptions(optionsJson: String)
    external fun nativeGetDefaultOptions(): String

    external fun nativeSetSystemPrompt(systemPrompt: String)
    external fun nativeGetSystemPrompt(): String
    external fun nativeSetSessionSystemPrompt(sessionId: String, systemPrompt: String): Boolean

    external fun nativeSetProviderConfig(providerId: String, apiKey: String, baseUrl: String, enabled: Boolean)
    external fun nativeGetProviderConfig(providerId: String): String
    external fun nativeListProviderConfigs(): String
    external fun nativeListProvidersModels(): String

    external fun nativeSetSessionWebSearch(sessionId: String, enabled: Boolean): Boolean
    external fun nativeSetSearchProvider(provider: String)
    external fun nativeSetExaApiKey(apiKey: String)
    external fun nativeSetToolExecutor(sessionId: String, callback: ToolExecutorCallback?)
    external fun nativeCancelRequest()

    external fun nativeRagInit(modelPath: String): String
    external fun nativeRagAddFile(sessionId: String, filePath: String, textContent: String): String
    external fun nativeRagSearch(sessionId: String, query: String, topK: Int, scopeFile: String?): String
    external fun nativeRagChunkCount(sessionId: String): Int
    external fun nativeRagMemoryBytes(sessionId: String): Long
    external fun nativeRagRemoveSession(sessionId: String)
    external fun nativeRagSetDebugCallback(callback: RagDebugCallback?)

    //  Space (RAG-enabled session) 
    external fun nativeSetSessionRag(sessionId: String, enabled: Boolean): Boolean
    external fun nativeInitRagModel(modelDir: String)
    external fun nativeSetProjectionEnabled(enabled: Boolean)
    external fun nativeSetRagConfig(configJson: String)
    external fun nativeGetRagConfig(): String
    external fun nativeAddSpaceFile(sessionId: String, filePath: String, textContent: String): String
    external fun nativeListSpaceFiles(sessionId: String): String
    external fun nativeExtractFileText(filePath: String): String
    external fun nativeSaveRagSession(sessionId: String, dirPath: String): Boolean
    external fun nativeLoadRagSession(sessionId: String, dirPath: String): Boolean
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

interface RagDebugCallback {
    fun onDebug(message: String)
}
