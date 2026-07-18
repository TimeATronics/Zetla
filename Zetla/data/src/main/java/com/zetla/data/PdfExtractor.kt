package com.zetla.data

import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import android.util.Base64
import android.util.Log
import com.tom_roush.pdfbox.android.PDFBoxResourceLoader
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import com.tom_roush.pdfbox.pdmodel.PDDocument
import com.tom_roush.pdfbox.rendering.PDFRenderer
import com.tom_roush.pdfbox.text.PDFTextStripper
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileInputStream

object PdfExtractor {
    private const val TAG = "PdfExtractor"
    private const val MAX_IMAGE_DIMENSION = 2048
    private const val JPEG_QUALITY = 85

    data class PdfResult(
        val text: String,
        val imageDataUris: List<String> = emptyList(),
        val pageCount: Int = 0
    )

    fun init(context: Context) {
        PDFBoxResourceLoader.init(context)
    }

    suspend fun extract(context: Context, uri: Uri): PdfResult = withContext(Dispatchers.IO) {
        val inputStream = openUri(context, uri)
            ?: return@withContext PdfResult(text = "[Failed to open PDF file from URI]", pageCount = 0)

        inputStream.use { stream ->
            val doc = PDDocument.load(stream)
            try {
                val pageCount = doc.numberOfPages

                val stripper = PDFTextStripper()
                val text = stripper.getText(doc)

                if (text.isBlank()) {
                    Log.w(TAG, "PDF text extraction yielded blank text, pages=$pageCount")
                }

                PdfResult(
                    text = if (text.isBlank()) "[PDF contained no extractable text]" else text,
                    imageDataUris = emptyList<String>(),
                    pageCount = pageCount
                )
            } catch (e: Exception) {
                Log.e(TAG, "PDF extraction failed", e)
                PdfResult(
                    text = "[PDF extraction failed: ${e.message}]",
                    imageDataUris = emptyList<String>(),
                    pageCount = 0
                )
            } finally {
                doc.close()
            }
        }
    }

    private fun openUri(context: Context, uri: Uri): java.io.InputStream? {
        if (uri.scheme == "content") {
            return context.contentResolver.openInputStream(uri)
        }
        return try {
            FileInputStream(File(uri.path!!))
        } catch (_: Exception) {
            null
        }
    }

    suspend fun extractWithImages(context: Context, uri: Uri): PdfResult = withContext(Dispatchers.IO) {
        val inputStream = openUri(context, uri)
            ?: return@withContext PdfResult(text = "[Failed to open PDF file from URI]", pageCount = 0)

        inputStream.use { stream ->
            val doc = PDDocument.load(stream)
            try {
                val pageCount = doc.numberOfPages

                val stripper = PDFTextStripper()
                val text = stripper.getText(doc)

                val renderer = PDFRenderer(doc)
                val imageUris = mutableListOf<String>()

                for (i in 0 until pageCount) {
                    try {
                        val page = doc.getPage(i)
                        val pageWidth = page.mediaBox.width
                        val pageHeight = page.mediaBox.height

                        val scale = minOf(
                            1.0f,
                            MAX_IMAGE_DIMENSION / pageWidth,
                            MAX_IMAGE_DIMENSION / pageHeight
                        )

                        val bitmap = renderer.renderImageWithDPI(i, 150f)
                        val dataUri = bitmapToDataUri(bitmap)
                        if (dataUri != null) {
                            imageUris.add(dataUri)
                        }
                        bitmap.recycle()
                    } catch (e: Exception) {
                        Log.w(TAG, "Failed to render page $i as image", e)
                    }
                }

                PdfResult(
                    text = if (text.isBlank()) "[PDF rendered as images - ${imageUris.size} pages]" else text,
                    imageDataUris = imageUris.toList(),
                    pageCount = pageCount
                )
            } catch (e: Exception) {
                Log.e(TAG, "PDF extraction with images failed", e)
                PdfResult(
                    text = "[PDF extraction failed: ${e.message}]",
                    imageDataUris = emptyList<String>(),
                    pageCount = 0
                )
            } finally {
                doc.close()
            }
        }
    }

    private fun bitmapToDataUri(bitmap: Bitmap): String? {
        return try {
            val outputStream = ByteArrayOutputStream()
            val scaled = scaleBitmap(bitmap)
            scaled.compress(Bitmap.CompressFormat.JPEG, JPEG_QUALITY, outputStream)
            val bytes = outputStream.toByteArray()
            val encoded = Base64.encodeToString(bytes, Base64.NO_WRAP)
            scaled.recycle()
            "data:image/jpeg;base64,$encoded"
        } catch (e: Exception) {
            Log.w(TAG, "Failed to convert bitmap to data URI", e)
            null
        }
    }

    private fun scaleBitmap(bitmap: Bitmap): Bitmap {
        val width = bitmap.width
        val height = bitmap.height
        if (width <= MAX_IMAGE_DIMENSION && height <= MAX_IMAGE_DIMENSION) return bitmap

        val ratio = minOf(MAX_IMAGE_DIMENSION.toFloat() / width, MAX_IMAGE_DIMENSION.toFloat() / height)
        val newWidth = (width * ratio).toInt()
        val newHeight = (height * ratio).toInt()
        return Bitmap.createScaledBitmap(bitmap, newWidth, newHeight, true)
    }
}
