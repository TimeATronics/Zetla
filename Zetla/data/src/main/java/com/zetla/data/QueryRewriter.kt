package com.zetla.data

object QueryRewriter {
    /**
     * Rewrite an LLM-generated query for better RAG retrieval.
     * Strips conversational filler, extracts core intent,
     * and generates alternative formulations.
     */
    fun rewrite(query: String): List<String> {
        val cleaned = cleanConversational(query)
        val alternatives = generateAlternatives(cleaned)
        return listOf(cleaned) + alternatives
    }

    private fun cleanConversational(query: String): String {
        return query
            .replace(Regex("(?i)can you (find|search|look for|tell me about)\\s+"), "")
            .replace(Regex("(?i)I need (to know|information about)\\s+"), "")
            .replace(Regex("(?i)what (is|are|does|do)\\s+"), "")
            .replace(Regex("(?i)please\\s+"), "")
            .replace(Regex("(?i)could you\\s+"), "")
            .replace(Regex("(?i)help me\\s+"), "")
            .replace(Regex("\\s+"), " ")
            .trim()
            .ifEmpty { query }
    }

    private fun generateAlternatives(query: String): List<String> {
        val words = query.split(" ").filter { it.length > 2 }
        if (words.size <= 2) return emptyList()

        // Alternative 1: key terms only (drop stop words)
        val stopWords = setOf("the", "and", "for", "with", "that", "this", "from", "have", "been", "about")
        val keyTerms = words.filter { it.lowercase() !in stopWords }.joinToString(" ")

        // Alternative 2: shorter version (first half of words)
        val half = words.take(words.size / 2 + 1).joinToString(" ")

        return listOf(keyTerms, half).filter { it.isNotBlank() && it != query }
    }
}
