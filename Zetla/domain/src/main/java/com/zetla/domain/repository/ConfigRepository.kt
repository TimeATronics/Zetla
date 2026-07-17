package com.zetla.domain.repository

interface ConfigRepository {
    companion object {
        const val DEFAULT_SYSTEM_PROMPT = "You are Zetla, a helpful and knowledgeable AI assistant. " +
            "Answer questions accurately, concisely, and thoughtfully. " +
            "When using tools, follow instructions precisely and return clear results."
    }

    fun setApiKey(apiKey: String)
    fun getApiKey(): String
    fun setModel(model: String)
    fun getModel(): String
    fun listProviders(): List<ProviderInfo>
    fun setProvider(providerId: String): Boolean
    fun getProvider(): String
    fun setDefaultParams(params: com.zetla.domain.model.ModelParams)
    fun getDefaultParams(): com.zetla.domain.model.ModelParams
    fun setSystemPrompt(prompt: String)
    fun getSystemPrompt(): String
    fun setProviderConfig(providerId: String, apiKey: String, baseUrl: String, enabled: Boolean)
    fun getProviderConfig(providerId: String): ProviderConfig
    fun listProviderConfigs(): List<ProviderConfig>
    fun fetchAllProviderModels(): List<com.zetla.domain.model.Model>
    fun init(): Boolean
    fun shutdown()
    fun getVersion(): String
    fun setDarkMode(enabled: Boolean)
    fun isDarkMode(): Boolean
    fun setSearchProvider(provider: String)
    fun setExaApiKey(apiKey: String)
    fun refreshModelsDevCache(force: Boolean): Boolean
    fun ensureModelsDevCached()
}

data class ProviderInfo(
    val id: String,
    val name: String,
    val defaultBaseUrl: String = ""
)

data class ProviderConfig(
    val providerId: String,
    val apiKey: String = "",
    val baseUrl: String = "",
    val enabled: Boolean = true
)
