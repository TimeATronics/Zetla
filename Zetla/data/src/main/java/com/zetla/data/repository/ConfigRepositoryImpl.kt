package com.zetla.data.repository

import android.content.Context
import android.content.SharedPreferences
import com.zetla.data.ModelsDevCache
import com.zetla.data.ZetlaCore
import com.zetla.domain.model.ModelParams
import com.zetla.domain.repository.ConfigRepository
import com.zetla.domain.repository.ProviderConfig
import com.zetla.domain.repository.ProviderInfo
import dagger.hilt.android.qualifiers.ApplicationContext
import org.json.JSONArray
import org.json.JSONObject
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class ConfigRepositoryImpl @Inject constructor(
    private val sharedPreferences: SharedPreferences,
    @ApplicationContext private val context: Context,
    private val modelsDevCache: ModelsDevCache
) : ConfigRepository {

    companion object {
        private const val KEY_API_KEY = "api_key"
        private const val KEY_MODEL = "model"
        private const val KEY_PROVIDER = "provider"
        private const val KEY_DARK_MODE = "dark_mode"
        private const val KEY_DEFAULT_PARAMS = "default_params"
        private const val KEY_SYSTEM_PROMPT = "system_prompt"
        private const val KEY_PROVIDER_CONFIGS = "provider_configs"
        private const val KEY_EXA_API_KEY = "exa_api_key"
        private const val DEFAULT_MODEL = ""
        private const val DEFAULT_PROVIDER = ""
    }

    override fun setApiKey(apiKey: String) {
        sharedPreferences.edit().putString(KEY_API_KEY, apiKey).apply()
        ZetlaCore.nativeSetApiKey(apiKey)
    }

    override fun getApiKey(): String {
        return sharedPreferences.getString(KEY_API_KEY, "") ?: ""
    }

    override fun setModel(model: String) {
        sharedPreferences.edit().putString(KEY_MODEL, model).apply()
        ZetlaCore.nativeSetModel(model)
    }

    override fun getModel(): String {
        return sharedPreferences.getString(KEY_MODEL, DEFAULT_MODEL) ?: DEFAULT_MODEL
    }

    override fun listProviders(): List<ProviderInfo> {
        return try {
            val json = ZetlaCore.nativeListProviders()
            val arr = JSONArray(json)
            (0 until arr.length()).mapNotNull { i ->
                val obj = arr.getJSONObject(i)
                ProviderInfo(
                    id = obj.optString("id", ""),
                    name = obj.optString("name", ""),
                    defaultBaseUrl = obj.optString("default_base_url", "")
                )
            }
        } catch (_: Exception) {
            emptyList()
        }
    }

    override fun setProvider(providerId: String): Boolean {
        sharedPreferences.edit().putString(KEY_PROVIDER, providerId).apply()
        return try {
            val json = ZetlaCore.nativeSetProvider(providerId)
            JSONObject(json).optBoolean("success", false)
        } catch (_: Exception) {
            true
        }
    }

    override fun getProvider(): String {
        return sharedPreferences.getString(KEY_PROVIDER, DEFAULT_PROVIDER) ?: DEFAULT_PROVIDER
    }

    override fun init(): Boolean {
        ZetlaCore.nativeSetApiKey(getApiKey())
        ZetlaCore.nativeSetModel(getModel())
        return try {
            val storagePath = context.filesDir.absolutePath + "/sessions"
            val json = ZetlaCore.nativeInitWithPath(storagePath)
            val ok = JSONObject(json).optBoolean("success", false)
            if (ok) {
                val sp = getSystemPrompt()
                if (sp.isNotBlank()) {
                    ZetlaCore.nativeSetSystemPrompt(sp)
                }
            val configs = loadProviderConfigsFromPrefs()
                    for (config in configs) {
                        if (config.apiKey.isNotBlank()) {
                            ZetlaCore.nativeSetProviderConfig(config.providerId, config.apiKey, config.baseUrl, config.enabled)
                        }
                    }
                    setProvider(getProvider())
                    // Configure EXA MCP search provider for web search tool
                    val exaKey = sharedPreferences.getString(KEY_EXA_API_KEY, "") ?: ""
                    ZetlaCore.nativeSetExaApiKey(exaKey)
                    ZetlaCore.nativeSetSearchProvider("exa")
            }
            ok
        } catch (_: Exception) {
            false
        }
    }

    override fun shutdown() {
        ZetlaCore.nativeShutdown()
    }

    override fun getVersion(): String {
        return try {
            ZetlaCore.nativeVersion()
        } catch (_: Exception) {
            "unknown"
        }
    }

    override fun setDefaultParams(params: ModelParams) {
        val json = params.toOptionsJson()
        sharedPreferences.edit().putString(KEY_DEFAULT_PARAMS, json).apply()
        try {
            ZetlaCore.nativeSetDefaultOptions(json)
        } catch (_: Throwable) {
            // native library not updated yet
        }
    }

    override fun getDefaultParams(): ModelParams {
        val json = sharedPreferences.getString(KEY_DEFAULT_PARAMS, "") ?: ""
        if (json.isNotBlank()) return ModelParams.fromOptionsJson(json)
        return try {
            val nativeJson = ZetlaCore.nativeGetDefaultOptions()
            if (nativeJson.isNotBlank() && nativeJson != "{}") {
                ModelParams.fromOptionsJson(nativeJson)
            } else ModelParams()
        } catch (_: Throwable) {
            ModelParams()
        }
    }

    override fun setDarkMode(enabled: Boolean) {
        sharedPreferences.edit().putBoolean(KEY_DARK_MODE, enabled).apply()
    }

    override fun isDarkMode(): Boolean {
        return sharedPreferences.getBoolean(KEY_DARK_MODE, true)
    }

    override fun setSystemPrompt(prompt: String) {
        sharedPreferences.edit().putString(KEY_SYSTEM_PROMPT, prompt).apply()
        try {
            ZetlaCore.nativeSetSystemPrompt(prompt)
        } catch (_: Throwable) {
            // native library not updated yet
        }
    }

    override fun getSystemPrompt(): String {
        val stored = sharedPreferences.getString(KEY_SYSTEM_PROMPT, "") ?: ""
        if (stored.isNotBlank()) return stored
        return try {
            val json = ZetlaCore.nativeGetSystemPrompt()
            val obj = JSONObject(json)
            val nativePrompt = obj.optString("system_prompt", "")
            if (nativePrompt.isNotBlank()) nativePrompt else ConfigRepository.DEFAULT_SYSTEM_PROMPT
        } catch (_: Throwable) {
            ConfigRepository.DEFAULT_SYSTEM_PROMPT
        }
    }

    override fun setProviderConfig(providerId: String, apiKey: String, baseUrl: String, enabled: Boolean) {
        ZetlaCore.nativeSetProviderConfig(providerId, apiKey, baseUrl, enabled)
        val configs = loadProviderConfigsFromPrefs().toMutableList()
        val idx = configs.indexOfFirst { it.providerId == providerId }
        val updated = ProviderConfig(providerId = providerId, apiKey = apiKey, baseUrl = baseUrl, enabled = enabled)
        if (idx >= 0) configs[idx] = updated else configs.add(updated)
        saveProviderConfigsToPrefs(configs)
    }

    override fun getProviderConfig(providerId: String): ProviderConfig {
        return try {
            val json = ZetlaCore.nativeGetProviderConfig(providerId)
            val obj = JSONObject(json)
            ProviderConfig(
                providerId = obj.optString("provider_id", providerId),
                apiKey = obj.optString("api_key", ""),
                baseUrl = obj.optString("base_url", ""),
                enabled = obj.optBoolean("enabled", false)
            )
        } catch (_: Throwable) {
            ProviderConfig(providerId = providerId)
        }
    }

    override fun listProviderConfigs(): List<ProviderConfig> {
        return loadProviderConfigsFromPrefs()
    }

    private fun loadProviderConfigsFromPrefs(): List<ProviderConfig> {
        val json = sharedPreferences.getString(KEY_PROVIDER_CONFIGS, "") ?: ""
        if (json.isBlank()) return emptyList()
        return try {
            val arr = JSONArray(json)
            (0 until arr.length()).mapNotNull { i ->
                val obj = arr.getJSONObject(i)
                ProviderConfig(
                    providerId = obj.optString("provider_id", ""),
                    apiKey = obj.optString("api_key", ""),
                    baseUrl = obj.optString("base_url", ""),
                    enabled = obj.optBoolean("enabled", false)
                )
            }
        } catch (_: Throwable) {
            emptyList()
        }
    }

    private fun saveProviderConfigsToPrefs(configs: List<ProviderConfig>) {
        val arr = JSONArray()
        for (c in configs) {
            val obj = JSONObject()
            obj.put("provider_id", c.providerId)
            obj.put("api_key", c.apiKey)
            obj.put("base_url", c.baseUrl)
            obj.put("enabled", c.enabled)
            arr.put(obj)
        }
        sharedPreferences.edit().putString(KEY_PROVIDER_CONFIGS, arr.toString()).apply()
    }

    override fun setSearchProvider(provider: String) {
        ZetlaCore.nativeSetSearchProvider(provider)
    }

    override fun setExaApiKey(apiKey: String) {
        sharedPreferences.edit().putString(KEY_EXA_API_KEY, apiKey).apply()
        ZetlaCore.nativeSetExaApiKey(apiKey)
    }

    override fun fetchAllProviderModels(): List<com.zetla.domain.model.Model> {
        return try {
            val json = ZetlaCore.nativeListProvidersModels()
            val arr = JSONArray(json)
            val models = (0 until arr.length()).mapNotNull { i ->
                val obj = arr.getJSONObject(i)
                val id = obj.optString("id", "")
                if (id.isNotEmpty()) {
                    val caps = if (obj.has("capabilities")) {
                        com.zetla.domain.model.ModelCapabilities.fromJson(obj.getJSONObject("capabilities"))
                    } else com.zetla.domain.model.ModelCapabilities()
                    com.zetla.domain.model.Model(
                        id = id,
                        name = obj.optString("name", id),
                        provider = obj.optString("provider", ""),
                        capabilities = caps
                    )
                } else null
            }
            enrichFromModelsDev(models)
        } catch (_: Exception) {
            emptyList()
        }
    }

    private fun enrichFromModelsDev(models: List<com.zetla.domain.model.Model>): List<com.zetla.domain.model.Model> {
        return models.map { model ->
            model.copy(
                capabilities = modelsDevCache.enrichCapabilities(model.id, model.provider, model.capabilities)
            )
        }
    }

    override fun refreshModelsDevCache(force: Boolean): Boolean {
        return modelsDevCache.refresh(force)
    }

    override fun ensureModelsDevCached() {
        if (modelsDevCache.getFlatIndex() == null) {
            modelsDevCache.refresh()
        }
    }
}
