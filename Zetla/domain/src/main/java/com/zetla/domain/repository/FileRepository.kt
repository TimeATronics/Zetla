package com.zetla.domain.repository

import android.net.Uri
import com.zetla.domain.model.FileAttachment
import com.zetla.domain.model.ZetlaResult

interface FileRepository {
    fun addFile(sessionId: String, filePath: String): ZetlaResult<FileAttachment>
    fun removeFile(sessionId: String, fileId: String): ZetlaResult<Unit>
    fun listFiles(sessionId: String): ZetlaResult<List<FileAttachment>>
    suspend fun extractPdfText(uri: Uri): ZetlaResult<String>
    suspend fun extractPdfWithImages(uri: Uri): ZetlaResult<PdfExtractResult>
}

data class PdfExtractResult(
    val text: String,
    val imageDataUris: List<String>
)
