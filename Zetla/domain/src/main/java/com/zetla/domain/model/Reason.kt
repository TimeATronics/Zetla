package com.zetla.domain.model

enum class Reason(val effort: String?) {
    None(effort = null),
    Low(effort = "low"),
    Medium(effort = "medium"),
    High(effort = "high")
}