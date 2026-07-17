package com.zetla.ui.voice

import android.content.Context
import android.util.Log
import org.vosk.Model
import org.vosk.Recognizer
import org.vosk.android.RecognitionListener
import org.vosk.android.SpeechService
import java.io.File
import java.io.IOException

class VoiceRecognitionService(private val context: Context) {

    private var model: Model? = null
    private var speechService: SpeechService? = null
    private var recognizer: Recognizer? = null
    private var isModelLoaded = false

    private val TAG = "VoiceRecognition"

    interface VoiceCallback {
        fun onPartialResult(text: String)
        fun onResult(text: String)
        fun onError(error: String)
        fun onReady()
    }

    fun loadModel(callback: VoiceCallback) {
        if (isModelLoaded) {
            callback.onReady()
            return
        }

        try {
            // Copy model from assets to internal storage if not already copied
            val modelDir = File(context.filesDir, "vosk_model")
            if (!modelDir.exists() || modelDir.listFiles()?.isEmpty() == true) {
                modelDir.mkdirs()
                val assetDir = "model-en-us"
                val assets = context.assets.list(assetDir)
                if (assets == null || assets.isEmpty()) {
                    callback.onError("Voice model not available. Bundle 'model-en-us' in app/src/main/assets/")
                    return
                }
                copyAssetDirectory(context, assetDir, modelDir)
                Log.d(TAG, "Copied model from assets/$assetDir to $modelDir")
            }

            model = Model(modelDir.absolutePath)
            isModelLoaded = true
            callback.onReady()
            Log.d(TAG, "Model loaded successfully from $modelDir")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load model", e)
            callback.onError("Failed to load voice model: ${e.message}")
        }
    }

    private fun copyAssetDirectory(context: Context, assetPath: String, outputDir: File) {
        val assets = context.assets.list(assetPath) ?: return
        for (item in assets) {
            val fullAssetPath = "$assetPath/$item"
            val outputFile = File(outputDir, item)
            if (context.assets.list(fullAssetPath)?.isNotEmpty() == true) {
                outputFile.mkdirs()
                copyAssetDirectory(context, fullAssetPath, outputFile)
            } else {
                context.assets.open(fullAssetPath).use { input ->
                    outputFile.outputStream().use { output ->
                        input.copyTo(output)
                    }
                }
            }
        }
    }

    fun startListening(listener: RecognitionListener) {
        val currentModel = model ?: return
        try {
            recognizer = Recognizer(currentModel, 16000.0f)
            speechService = SpeechService(recognizer, 16000.0f)
            speechService?.startListening(listener)
            Log.d(TAG, "Started listening")
        } catch (e: IOException) {
            Log.e(TAG, "Failed to start listening", e)
        }
    }

    fun stopListening() {
        speechService?.stop()
        speechService = null
        recognizer?.close()
        recognizer = null
        Log.d(TAG, "Stopped listening")
    }

    fun isListening(): Boolean = speechService != null

    fun destroy() {
        stopListening()
        model?.close()
        model = null
        isModelLoaded = false
    }
}
