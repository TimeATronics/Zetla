package com.zetla.domain.repository

import com.zetla.domain.model.FileAttachment
import com.zetla.domain.model.ZetlaResult

interface FileRepository {
    fun addFile(sessionId: String, filePath: String): ZetlaResult<FileAttachment>
    fun removeFile(sessionId: String, fileId: String): ZetlaResult<Unit>
    fun listFiles(sessionId: String): ZetlaResult<List<FileAttachment>>
}
