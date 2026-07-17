package com.zetla.ui.screens.chat.components

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.sp

@Composable
fun MarkdownText(
    markdown: String,
    modifier: Modifier = Modifier,
    color: Color = MaterialTheme.colorScheme.onSurface,
    maxLines: Int = Int.MAX_VALUE
) {
    val annotatedString = remember(markdown, color) {
        parseMarkdown(markdown, color)
    }
    Text(
        text = annotatedString,
        modifier = modifier.fillMaxWidth(),
        color = color,
        lineHeight = 20.sp,
        maxLines = maxLines
    )
}

private const val CODE_BG_ALPHA = 0.12f
private const val CODE_FONT_SIZE = 13f
private const val H1_SIZE = 22f
private const val H2_SIZE = 20f
private const val H3_SIZE = 18f
private const val H4_SIZE = 16f
private const val H5_SIZE = 14f
private const val H6_SIZE = 13f
private const val BULLET = "\u2022 "
private const val HR_CHAR = "\u2500"

private fun parseMarkdown(markdown: String, baseColor: Color): AnnotatedString {
    return buildAnnotatedString {
        val linkColor = Color(0xFF6C63FF)
        val codeBgColor = baseColor.copy(alpha = CODE_BG_ALPHA)
        val lines = markdown.split("\n")
        var i = 0
        while (i < lines.size) {
            if (i > 0 && !(lines[i - 1].startsWith("```"))) append("\n")
            val line = lines[i]

            // Fenced code block
            if (line.trimStart().startsWith("```")) {
                val lang = line.trimStart().substring(3).trim()
                val sb = StringBuilder()
                i++
                while (i < lines.size && !lines[i].trimStart().startsWith("```")) {
                    if (sb.isNotEmpty()) sb.append("\n")
                    sb.append(lines[i])
                    i++
                }
                val code = sb.toString()
                withStyle(SpanStyle(fontFamily = FontFamily.Monospace, background = codeBgColor, fontSize = CODE_FONT_SIZE.sp)) {
                    append("\n $code\n")
                }
                if (i < lines.size) i++ // skip closing ```
                continue
            }

            // Horizontal rule
            if (line.matches(Regex("^\\s*[-*_]{3,}\\s*$"))) {
                withStyle(SpanStyle(color = baseColor.copy(alpha = 0.3f))) {
                    append(HR_CHAR.repeat(30))
                }
                i++
                continue
            }

            // Blockquote
            val bqMatch = Regex("^ {0,3}>{1,3}\\s+(.*)").find(line)
            if (bqMatch != null) {
                val quoteText = bqMatch.groupValues[1]
                withStyle(SpanStyle(color = baseColor.copy(alpha = 0.7f), fontStyle = FontStyle.Italic)) {
                    append("\u275D ")
                }
                appendInlineFormatted(quoteText, baseColor, linkColor)
                i++
                continue
            }

            // Headers
            val hMatch = Regex("^(#{1,6})\\s+(.*)").find(line)
            if (hMatch != null) {
                val level = hMatch.groupValues[1].length
                val headerText = hMatch.groupValues[2].trim()
                val hSize = when (level) {
                    1 -> H1_SIZE
                    2 -> H2_SIZE
                    3 -> H3_SIZE
                    4 -> H4_SIZE
                    5 -> H5_SIZE
                    else -> H6_SIZE
                }
                withStyle(SpanStyle(fontSize = hSize.sp, fontWeight = FontWeight.Bold)) {
                    appendInlineFormatted(headerText, baseColor, linkColor)
                }
                i++
                continue
            }

            // Unordered list
            val ulMatch = Regex("^\\s*[-*+]\\s+(.*)").find(line)
            if (ulMatch != null) {
                val itemText = ulMatch.groupValues[1]
                withStyle(SpanStyle(fontWeight = FontWeight.Normal)) {
                    append(BULLET)
                }
                appendInlineFormatted(itemText, baseColor, linkColor)
                i++
                continue
            }

            // Ordered list
            val olMatch = Regex("^\\s*\\d+\\.\\s+(.*)").find(line)
            if (olMatch != null) {
                val itemText = olMatch.groupValues[1]
                appendInlineFormatted(itemText, baseColor, linkColor)
                i++
                continue
            }

            // Regular line
            appendInlineFormatted(line, baseColor, linkColor)
            i++
        }
    }
}

private fun AnnotatedString.Builder.appendInlineFormatted(
    text: String,
    baseColor: Color,
    linkColor: Color
) {
    val codeBgColor = baseColor.copy(alpha = CODE_BG_ALPHA)
    var i = 0
    while (i < text.length) {
        when {
            // Backslash escape
            text[i] == '\\' && i + 1 < text.length -> {
                val next = text[i + 1]
                if (next in "\\*`_~[]()-#+.!") {
                    append(next)
                    i += 2
                } else {
                    append(text[i])
                    i++
                }
            }
            // Code block ```...``` (within a line, for completeness)
            text.startsWith("```", i) -> {
                val end = text.indexOf("```", i + 3)
                if (end >= 0) {
                    val code = text.substring(i + 3, end).trimStart('\n').trimEnd('\n')
                    withStyle(SpanStyle(fontFamily = FontFamily.Monospace, background = codeBgColor, fontSize = CODE_FONT_SIZE.sp)) {
                        append("\n$code\n")
                    }
                    i = end + 3
                } else {
                    append(text[i]); i++
                }
            }
            // Inline code `...`
            text[i] == '`' -> {
                val end = text.indexOf('`', i + 1)
                if (end >= 0) {
                    val code = text.substring(i + 1, end)
                    withStyle(SpanStyle(fontFamily = FontFamily.Monospace, background = codeBgColor, fontSize = CODE_FONT_SIZE.sp)) {
                        append(code)
                    }
                    i = end + 1
                } else {
                    append(text[i]); i++
                }
            }
            // Bold italic ***...***
            (text.startsWith("***", i) || text.startsWith("___", i)) -> {
                val marker = text.substring(i, i + 3)
                val end = text.indexOf(marker, i + 3)
                if (end >= 0) {
                    val inner = text.substring(i + 3, end)
                    withStyle(SpanStyle(fontWeight = FontWeight.Bold, fontStyle = FontStyle.Italic)) {
                        append(inner)
                    }
                    i = end + 3
                } else {
                    append(text[i]); i++
                }
            }
            // Bold **...** or __...__
            (text.startsWith("**", i) || text.startsWith("__", i)) -> {
                val marker = text.substring(i, i + 2)
                val end = text.indexOf(marker, i + 2)
                if (end >= 0) {
                    val inner = text.substring(i + 2, end)
                    withStyle(SpanStyle(fontWeight = FontWeight.Bold)) {
                        append(inner)
                    }
                    i = end + 2
                } else {
                    append(text[i]); i++
                }
            }
            // Italic *...* (single asterisk, not **)
            text[i] == '*' && i + 1 < text.length && text[i + 1] != '*' -> {
                val end = text.indexOf('*', i + 1)
                if (end >= 0 && end > i + 1) {
                    val inner = text.substring(i + 1, end)
                    withStyle(SpanStyle(fontStyle = FontStyle.Italic)) {
                        append(inner)
                    }
                    i = end + 1
                } else {
                    append(text[i]); i++
                }
            }
            // Italic _..._ (single underscore, not __)
            text[i] == '_' && i + 1 < text.length && text[i + 1] != '_' -> {
                val end = text.indexOf('_', i + 1)
                if (end >= 0 && end > i + 1) {
                    val inner = text.substring(i + 1, end)
                    withStyle(SpanStyle(fontStyle = FontStyle.Italic)) {
                        append(inner)
                    }
                    i = end + 1
                } else {
                    append(text[i]); i++
                }
            }
            // Strikethrough ~~...~~
            text.startsWith("~~", i) -> {
                val end = text.indexOf("~~", i + 2)
                if (end >= 0) {
                    val inner = text.substring(i + 2, end)
                    withStyle(SpanStyle(textDecoration = TextDecoration.LineThrough)) {
                        append(inner)
                    }
                    i = end + 2
                } else {
                    append(text[i]); i++
                }
            }
            // Link [text](url)
            text[i] == '[' -> {
                val closeBracket = text.indexOf(']', i + 1)
                if (closeBracket >= 0 && closeBracket + 1 < text.length && text[closeBracket + 1] == '(') {
                    val closeParen = text.indexOf(')', closeBracket + 2)
                    if (closeParen >= 0) {
                        val linkText = text.substring(i + 1, closeBracket)
                        withStyle(SpanStyle(color = linkColor, textDecoration = TextDecoration.Underline)) {
                            append(linkText)
                        }
                        i = closeParen + 1
                    } else {
                        append(text[i]); i++
                    }
                } else {
                    append(text[i]); i++
                }
            }
            else -> {
                append(text[i])
                i++
            }
        }
    }
}
