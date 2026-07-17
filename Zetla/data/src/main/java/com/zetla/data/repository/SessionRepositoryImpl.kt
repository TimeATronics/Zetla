package com.zetla.data.repository

import com.zetla.data.ZetlaCore
import com.zetla.data.mapper.toSession
import com.zetla.data.mapper.toSessionList
import com.zetla.domain.model.Session
import com.zetla.domain.model.ZetlaResult
import com.zetla.domain.repository.SessionRepository
import org.json.JSONObject
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class SessionRepositoryImpl @Inject constructor() : SessionRepository {

    override fun createSession(model: String, systemPrompt: String): ZetlaResult<Session> {
        return try {
            val json = ZetlaCore.nativeCreateSession(model, systemPrompt)
            val obj = JSONObject(json)
            if (obj.optBoolean("success", false)) {
                val session = json.toSession()
                if (session != null) ZetlaResult.success(session)
                else ZetlaResult.error("Failed to parse session")
            } else {
                ZetlaResult.error(obj.optString("error", "Create session failed"))
            }
        } catch (e: Exception) {
            ZetlaResult.error(e.message ?: "Exception")
        }
    }

    override fun deleteSession(sessionId: String): ZetlaResult<Unit> {
        return try {
            val json = ZetlaCore.nativeDeleteSession(sessionId)
            val obj = JSONObject(json)
            if (obj.optBoolean("success", false)) ZetlaResult.success(Unit)
            else ZetlaResult.error(obj.optString("error", "Delete failed"))
        } catch (e: Exception) {
            ZetlaResult.error(e.message ?: "Exception")
        }
    }

    override fun getSessionInfo(sessionId: String): ZetlaResult<Session> {
        return try {
            val json = ZetlaCore.nativeGetSessionInfo(sessionId)
            val obj = JSONObject(json)
            if (obj.optBoolean("success", false)) {
                val session = json.toSession()
                if (session != null) ZetlaResult.success(session)
                else ZetlaResult.error("Parse failed")
            } else {
                ZetlaResult.error(obj.optString("error", "Not found"))
            }
        } catch (e: Exception) {
            ZetlaResult.error(e.message ?: "Exception")
        }
    }

    override fun listSessions(): ZetlaResult<List<Session>> {
        return try {
            val json = ZetlaCore.nativeListSessions()
            val obj = JSONObject(json)
            if (obj.optBoolean("success", false)) {
                ZetlaResult.success(json.toSessionList())
            } else {
                ZetlaResult.error(obj.optString("error", "List failed"))
            }
        } catch (e: Exception) {
            ZetlaResult.error(e.message ?: "Exception")
        }
    }

    override fun loadSession(sessionId: String): ZetlaResult<Session> {
        return try {
            val json = ZetlaCore.nativeLoadSession(sessionId)
            val obj = JSONObject(json)
            if (obj.optBoolean("success", false)) {
                val session = json.toSession()
                if (session != null) ZetlaResult.success(session)
                else ZetlaResult.error("Parse failed")
            } else {
                ZetlaResult.error(obj.optString("error", "Load failed"))
            }
        } catch (e: Exception) {
            ZetlaResult.error(e.message ?: "Exception")
        }
    }

    override fun sessionExistsOnDisk(sessionId: String): Boolean {
        return try {
            val json = ZetlaCore.nativeSessionExistsOnDisk(sessionId)
            JSONObject(json).optBoolean("exists", false)
        } catch (_: Exception) {
            false
        }
    }

    override fun deleteFromStorage(sessionId: String): ZetlaResult<Unit> {
        return try {
            val json = ZetlaCore.nativeDeleteFromStorage(sessionId)
            val obj = JSONObject(json)
            if (obj.optBoolean("success", false)) ZetlaResult.success(Unit)
            else ZetlaResult.error(obj.optString("error", "Delete failed"))
        } catch (e: Exception) {
            ZetlaResult.error(e.message ?: "Exception")
        }
    }

    override fun compactSession(sessionId: String): ZetlaResult<String> {
        return try {
            val json = ZetlaCore.nativeCompactSession(sessionId)
            val obj = JSONObject(json)
            if (obj.optBoolean("success", false)) {
                ZetlaResult.success(obj.optString("data", ""))
            } else {
                ZetlaResult.error(obj.optString("error", "Compact failed"))
            }
        } catch (e: Exception) {
            ZetlaResult.error(e.message ?: "Exception")
        }
    }
}
