package com.zetla.domain.model

data class FileAttachment(
    val id: String,
    val name: String,
    val path: String,
    val type: FileType,
    val size: Long = 0
)

enum class FileType {
    TEXT, PDF, IMAGE, SPREADSHEET, PRESENTATION, DOCUMENT, UNKNOWN
}
