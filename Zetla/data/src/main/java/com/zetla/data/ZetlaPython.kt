package com.zetla.data

import android.content.Context
import android.util.Log
import java.io.File

object ZetlaPython {
    private const val TAG = "ZetlaPython"
    private var initialized = false
    private var initializing = false
    private var pythonBinPath: String? = null
    private var filesDir: String? = null
    private var appContext: Context? = null

    fun init(context: Context) {
        if (initialized) return
        appContext = context
    }

    private fun ensureInitialized(): Boolean {
        if (initialized) return true
        if (initializing) return false
        initializing = true

        val ctx = appContext ?: return false
        try {
            filesDir = ctx.filesDir.absolutePath
            val nativeLibDir = ctx.applicationInfo.nativeLibraryDir
            val bin = File(nativeLibDir, "libpython.so")
            if (!bin.exists()) {
                Log.w(TAG, "libpython.so not found at $nativeLibDir")
                initializing = false
                return false
            }
            pythonBinPath = bin.absolutePath
            Log.d(TAG, "Python binary: ${bin.absolutePath} (${bin.length()} bytes)")

            val zipDst = File(ctx.filesDir, "python314t.zip")
            if (!zipDst.exists()) {
                Log.d(TAG, "Extracting python314t.zip...")
                ctx.assets.open("python314t.zip").use { src ->
                    zipDst.outputStream().use { out -> src.copyTo(out) }
                }
                Log.d(TAG, "Extracted python314t.zip (${zipDst.length()} bytes)")
            }

            val certDst = File(ctx.filesDir, "cacert.pem")
            if (!certDst.exists()) {
                ctx.assets.open("cacert.pem").use { src ->
                    certDst.outputStream().use { out -> src.copyTo(out) }
                }
            }

            initialized = true
            Log.d(TAG, "Python runtime initialized")
            return true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to initialize Python", e)
            return false
        } finally {
            initializing = false
        }
    }

    fun execute(script: String, timeoutMs: Long = 30_000): PythonResult {
        if (!ensureInitialized()) {
            return PythonResult(false, "", "Python not initialized")
        }

        Log.d(TAG, "execute: ${script.take(200)}...")
        val bin = pythonBinPath ?: return PythonResult(false, "", "Python binary not found")
        val dir = filesDir ?: return PythonResult(false, "", "Files dir not set")

        return try {
            val tmpScript = File(dir, "zetla_run.py")
            tmpScript.writeText(script)

            val process = ProcessBuilder(bin, tmpScript.absolutePath)
                .directory(File(dir))
                .redirectErrorStream(true)
                .apply {
                    val env = environment()
                    env["PYTHONHOME"] = dir
                    env["PYTHONPATH"] = "$dir/python314t.zip"
                    env["SSL_CERT_FILE"] = "$dir/cacert.pem"
                    env["HOME"] = dir
                    env["TMPDIR"] = dir
                }
                .start()

            val output = process.inputStream.bufferedReader().readText()
            val exited = process.waitFor(timeoutMs, java.util.concurrent.TimeUnit.MILLISECONDS)
            if (!exited) {
                process.destroyForcibly()
                PythonResult(false, output, "Timed out after ${timeoutMs}ms")
            } else {
                val code = process.exitValue()
                PythonResult(code == 0, output, if (code != 0) "Exit code: $code" else null)
            }
        } catch (e: Exception) {
            Log.e(TAG, "execute error", e)
            PythonResult(false, "", e.message ?: "Unknown error")
        }
    }
}

data class PythonResult(
    val success: Boolean,
    val output: String,
    val error: String? = null
)
