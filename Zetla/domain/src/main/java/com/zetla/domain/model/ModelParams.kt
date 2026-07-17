package com.zetla.domain.model

data class ModelParams(
    val temperature: Float = 0.7f,
    val maxTokens: Int = 2048,
    val topP: Float = 0.95f,
    val reasoningEffort: String = "",
    val frequencyPenalty: Float = 0.0f,
    val presencePenalty: Float = 0.0f,
    val seed: Int? = null
) {
    fun toOptionsJson(): String {
        val sb = StringBuilder("{")
        sb.append("\"temperature\":$temperature")
        sb.append(",\"max_tokens\":$maxTokens")
        sb.append(",\"top_p\":$topP")
        if (reasoningEffort.isNotBlank() && reasoningEffort != "none") {
            sb.append(",\"reasoning_effort\":\"$reasoningEffort\"")
        }
        if (frequencyPenalty != 0.0f) sb.append(",\"frequency_penalty\":$frequencyPenalty")
        if (presencePenalty != 0.0f) sb.append(",\"presence_penalty\":$presencePenalty")
        if (seed != null) sb.append(",\"seed\":$seed")
        sb.append("}")
        return sb.toString()
    }

    companion object {
        fun fromOptionsJson(json: String): ModelParams {
            val obj = try { org.json.JSONObject(json) } catch (_: Exception) { return ModelParams() }
            return ModelParams(
                temperature = obj.optDouble("temperature", 0.7).toFloat(),
                maxTokens = obj.optInt("max_tokens", 2048),
                topP = obj.optDouble("top_p", 0.95).toFloat(),
                reasoningEffort = obj.optString("reasoning_effort", ""),
                frequencyPenalty = obj.optDouble("frequency_penalty", 0.0).toFloat(),
                presencePenalty = obj.optDouble("presence_penalty", 0.0).toFloat(),
                seed = if (obj.has("seed")) obj.optInt("seed") else null
            )
        }
    }
}
