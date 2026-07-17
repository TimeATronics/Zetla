package com.zetla.data

import android.content.Context
import android.util.Log
import dagger.hilt.android.qualifiers.ApplicationContext
import org.json.JSONObject
import java.io.BufferedReader
import java.io.File
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL
import javax.inject.Inject
import javax.inject.Singleton

data class ReasoningOption(
    val type: String,
    val values: List<String> = emptyList()
)

data class ModelDevEntry(
    val id: String,
    val name: String,
    val contextWindow: Int,
    val maxOutputTokens: Int,
    val supportsVision: Boolean,
    val supportsTools: Boolean,
    val supportsReasoning: Boolean,
    val reasoningOptions: List<ReasoningOption> = emptyList()
)

@Singleton
class ModelsDevCache @Inject constructor(
    @ApplicationContext private val context: Context
) {
    companion object {
        private const val MODELS_DEV_URL = "https://models.dev/api.json"
        private const val CACHE_FILENAME = "models_dev.json"
        private const val TAG = "ModelsDevCache"
    }

    private val cacheFile = File(context.filesDir, CACHE_FILENAME)
    private var parsedCache: Map<String, ModelDevEntry>? = null

    private fun buildFlatIndex(json: JSONObject): Map<String, ModelDevEntry> {
        val map = mutableMapOf<String, ModelDevEntry>()
        val providerIds = json.keys()
        while (providerIds.hasNext()) {
            val providerId = providerIds.next()
            val providerObj = json.optJSONObject(providerId) ?: continue
            val models = providerObj.optJSONObject("models") ?: continue
            val modelKeys = models.keys()
            while (modelKeys.hasNext()) {
                val modelId = modelKeys.next()
                val modelObj = models.optJSONObject(modelId) ?: continue
                val entry = parseModelEntry(modelObj)
                map[modelId] = entry
                val compositeKey = "$providerId/$modelId"
                if (!map.containsKey(compositeKey)) {
                    map[compositeKey] = entry
                }
            }
        }
        return map
    }

    private fun parseModelEntry(obj: JSONObject): ModelDevEntry {
        val id = obj.optString("id", "")
        val name = obj.optString("name", id)

        val limit = obj.optJSONObject("limit")
        val contextWindow = limit?.optInt("context", 8192) ?: 8192
        val maxOutputTokens = limit?.optInt("output", 4096) ?: 4096

        val modalities = obj.optJSONObject("modalities")
        val supportsVision = modalities?.optJSONArray("input")?.let { arr ->
            (0 until arr.length()).any { arr.optString(it) == "image" }
        } ?: false

        val supportsTools = obj.optBoolean("tool_call", false)
        val supportsReasoning = obj.optBoolean("reasoning", false)

        val reasoningOptions = mutableListOf<ReasoningOption>()
        val roArr = obj.optJSONArray("reasoning_options")
        if (roArr != null) {
            for (i in 0 until roArr.length()) {
                val ro = roArr.optJSONObject(i) ?: continue
                val type = ro.optString("type", "")
                val values = mutableListOf<String>()
                val vals = ro.optJSONArray("values")
                if (vals != null) {
                    for (j in 0 until vals.length()) {
                        val v = vals.optString(j, null)
                        if (v != null) values.add(v)
                    }
                }
                reasoningOptions.add(ReasoningOption(type, values))
            }
        }

        return ModelDevEntry(
            id = id,
            name = name,
            contextWindow = contextWindow,
            maxOutputTokens = maxOutputTokens,
            supportsVision = supportsVision,
            supportsTools = supportsTools,
            supportsReasoning = supportsReasoning,
            reasoningOptions = reasoningOptions
        )
    }

    private fun fetchFromNetwork(): String? {
        return try {
            val url = URL(MODELS_DEV_URL)
            val conn = url.openConnection() as HttpURLConnection
            conn.requestMethod = "GET"
            conn.connectTimeout = 15000
            conn.readTimeout = 30000
            conn.instanceFollowRedirects = true
            if (conn.responseCode != 200) {
                Log.w(TAG, "Fetch failed: HTTP ${conn.responseCode}")
                return null
            }
            val reader = BufferedReader(InputStreamReader(conn.inputStream))
            val response = reader.readText()
            reader.close()
            conn.disconnect()
            response
        } catch (e: Exception) {
            Log.w(TAG, "Fetch error", e)
            null
        }
    }

    private fun loadFromFile(): String? {
        return try {
            if (cacheFile.exists()) cacheFile.readText() else null
        } catch (e: Exception) {
            Log.w(TAG, "Cache read error", e)
            null
        }
    }

    private fun saveToFile(text: String) {
        try {
            cacheFile.parentFile?.mkdirs()
            cacheFile.writeText(text)
        } catch (e: Exception) {
            Log.w(TAG, "Cache write error", e)
        }
    }

    @Synchronized
    fun getFlatIndex(): Map<String, ModelDevEntry>? {
        if (parsedCache != null) return parsedCache
        val jsonText = loadFromFile()
        if (jsonText != null) {
            parsedCache = buildFlatIndex(JSONObject(jsonText))
            return parsedCache
        }
        return null
    }

    fun refresh(force: Boolean = false): Boolean {
        if (!force && cacheFile.exists()) {
            getFlatIndex()
            return true
        }
        val text = fetchFromNetwork() ?: return false
        saveToFile(text)
        parsedCache = buildFlatIndex(JSONObject(text))
        return true
    }

    fun ensureFetched() {
        if (getFlatIndex() != null) return
        refresh()
    }

    fun enrichCapabilities(
        modelId: String,
        provider: String,
        current: com.zetla.domain.model.ModelCapabilities
    ): com.zetla.domain.model.ModelCapabilities {
        val index = getFlatIndex() ?: return current
        val entry = index[modelId] ?: index["$provider/$modelId"] ?: return current

        val thinkingLevels = entry.reasoningOptions
            .filter { it.type == "effort" }
            .flatMap { it.values }
            .distinct()

        return current.copy(
            supportsVision = current.supportsVision || entry.supportsVision,
            supportsTools = current.supportsTools || entry.supportsTools,
            supportsReasoning = current.supportsReasoning || entry.supportsReasoning,
            contextWindow = entry.contextWindow,
            maxOutputTokens = entry.maxOutputTokens,
            thinkingLevels = thinkingLevels
        )
    }

    fun deleteCache() {
        try {
            if (cacheFile.exists()) cacheFile.delete()
            parsedCache = null
        } catch (e: Exception) {
            Log.w(TAG, "Cache delete error", e)
        }
    }
}
