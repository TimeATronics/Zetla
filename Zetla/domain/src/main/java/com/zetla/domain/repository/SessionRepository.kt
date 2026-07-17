package com.zetla.domain.repository

import com.zetla.domain.model.Session
import com.zetla.domain.model.ZetlaResult

interface SessionRepository {
    fun createSession(model: String, systemPrompt: String): ZetlaResult<Session>
    fun deleteSession(sessionId: String): ZetlaResult<Unit>
    fun getSessionInfo(sessionId: String): ZetlaResult<Session>
    fun listSessions(): ZetlaResult<List<Session>>
    fun loadSession(sessionId: String): ZetlaResult<Session>
    fun sessionExistsOnDisk(sessionId: String): Boolean
    fun deleteFromStorage(sessionId: String): ZetlaResult<Unit>
    fun compactSession(sessionId: String): ZetlaResult<String>
}
