package com.zetla.domain.model

data class Model(
    val id: String,
    val name: String,
    val provider: String = "",
    val capabilities: ModelCapabilities = ModelCapabilities()
) {
    companion object {
        val defaultModel = Model("", "")
    }
}

data class ModelCapabilities(
    val supportsVision: Boolean = false,
    val supportsTools: Boolean = false,
    val supportsReasoning: Boolean = false,
    val contextWindow: Int = 8192,
    val maxOutputTokens: Int = 4096,
    val supportedParams: List<String> = emptyList(),
    val thinkingLevels: List<String> = emptyList()
) {
    companion object {
        fun fromJson(obj: org.json.JSONObject): ModelCapabilities {
            return ModelCapabilities(
                supportsVision = obj.optBoolean("supports_vision", false),
                supportsTools = obj.optBoolean("supports_tools", false),
                supportsReasoning = obj.optBoolean("supports_reasoning", false),
                contextWindow = obj.optInt("context_window", 8192),
                maxOutputTokens = obj.optInt("max_output_tokens", 4096),
                supportedParams = try {
                    val arr = obj.getJSONArray("supported_params")
                    (0 until arr.length()).map { arr.getString(it) }
                } catch (_: Exception) { emptyList() },
                thinkingLevels = try {
                    val arr = obj.getJSONArray("thinking_levels")
                    (0 until arr.length()).map { arr.getString(it) }
                } catch (_: Exception) { emptyList() }
            )
        }
    }
}
