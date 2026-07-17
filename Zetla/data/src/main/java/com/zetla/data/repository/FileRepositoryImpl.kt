package com.zetla.data.repository

import android.util.Log
import com.zetla.data.ZetlaCore
import com.zetla.data.mapper.toFileAttachments
import com.zetla.data.mapper.toFileAttachment
import com.zetla.domain.model.FileAttachment
import com.zetla.domain.model.ZetlaResult
import com.zetla.domain.repository.FileRepository
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class FileRepositoryImpl @Inject constructor() : FileRepository {

    private fun nativeId(sessionId: String): String = sessionId.replace("-", "")

    override fun addFile(sessionId: String, filePath: String): ZetlaResult<FileAttachment> {
        val nid = nativeId(sessionId)
        Log.d("FileRepo", "addFile: sid=$nid path=$filePath")
        return try {
            val json = ZetlaCore.nativeAddFile(nid, filePath)
            Log.d("FileRepo", "addFile response: $json")
            if (json.contains("\"file_id\"")) {
                val file = json.toFileAttachment()
                if (file != null) {
                    Log.d("FileRepo", "addFile success: id=${file.id}")
                    ZetlaResult.success(file)
                }
                else ZetlaResult.error("Parse failed")
            } else {
                Log.e("FileRepo", "addFile failed: $json")
                ZetlaResult.error("Add file failed: $json")
            }
        } catch (e: Exception) {
            Log.e("FileRepo", "addFile exception", e)
            ZetlaResult.error(e.message ?: "Exception")
        }
    }

    override fun removeFile(sessionId: String, fileId: String): ZetlaResult<Unit> {
        return try {
            val json = ZetlaCore.nativeRemoveFile(nativeId(sessionId), fileId)
            if (json.contains("\"file_id\"") || json.contains("\"status\""))
                ZetlaResult.success(Unit)
            else
                ZetlaResult.error("Remove failed: $json")
        } catch (e: Exception) {
            ZetlaResult.error(e.message ?: "Exception")
        }
    }

    override fun listFiles(sessionId: String): ZetlaResult<List<FileAttachment>> {
        return try {
            val json = ZetlaCore.nativeListFiles(nativeId(sessionId))
            if (json.contains("\"files\"")) {
                ZetlaResult.success(json.toFileAttachments())
            } else {
                ZetlaResult.error("List files failed: $json")
            }
        } catch (e: Exception) {
            ZetlaResult.error(e.message ?: "Exception")
        }
    }
}
