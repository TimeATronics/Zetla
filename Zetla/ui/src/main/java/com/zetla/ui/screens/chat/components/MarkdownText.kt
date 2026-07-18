package com.zetla.ui.screens.chat.components

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

private const val CODE_BG_ALPHA = 0.12f
private const val CODE_FONT_SIZE = 13f
private const val H1_SIZE = 22f
private const val H2_SIZE = 20f
private const val H3_SIZE = 18f
private const val H4_SIZE = 16f
private const val H5_SIZE = 14f
private const val H6_SIZE = 13f
private val BULLET = "\u2022 "
private val HR_CHAR = "\u2500"

private sealed class MarkdownBlock {
    data class Paragraph(val text: AnnotatedString) : MarkdownBlock()
    data class CodeBlock(val code: String, val language: String) : MarkdownBlock()
    data class Table(val rows: List<List<String>>) : MarkdownBlock()
    data class Heading(val level: Int, val text: String) : MarkdownBlock()
    data class Blockquote(val text: AnnotatedString) : MarkdownBlock()
    data class UnorderedListItem(val text: AnnotatedString) : MarkdownBlock()
    data object HorizontalRule : MarkdownBlock()
}

@Composable
fun MarkdownText(
    markdown: String,
    modifier: Modifier = Modifier,
    color: Color = MaterialTheme.colorScheme.onSurface,
    maxLines: Int = Int.MAX_VALUE
) {
    val blocks = remember(markdown) {
        parseBlocks(markdown, color)
    }

    Column(modifier = modifier.fillMaxWidth()) {
        blocks.forEach { block ->
            when (block) {
                is MarkdownBlock.Paragraph -> {
                    if (block.text.isNotEmpty()) {
                        Text(
                            text = block.text,
                            color = color,
                            lineHeight = 20.sp,
                            maxLines = maxLines
                        )
                    }
                }
                is MarkdownBlock.CodeBlock -> {
                    Spacer(Modifier.height(6.dp))
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(8.dp)),
                        color = color.copy(alpha = CODE_BG_ALPHA),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        val scrollState = rememberScrollState()
                        Text(
                            text = block.code,
                            fontFamily = FontFamily.Monospace,
                            fontSize = CODE_FONT_SIZE.sp,
                            color = color,
                            modifier = Modifier
                                .horizontalScroll(scrollState)
                                .padding(12.dp),
                            lineHeight = 18.sp
                        )
                    }
                    Spacer(Modifier.height(6.dp))
                }
                is MarkdownBlock.Table -> {
                    Spacer(Modifier.height(6.dp))
                    val scrollState = rememberScrollState()
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .horizontalScroll(scrollState)
                            .clip(RoundedCornerShape(8.dp)),
                        color = color.copy(alpha = CODE_BG_ALPHA * 0.5f),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Column(modifier = Modifier.padding(8.dp)) {
                            block.rows.forEachIndexed { rowIndex, row ->
                                Row {
                                    row.forEachIndexed { colIndex, cell ->
                                        val isHeader = rowIndex == 0
                                        Text(
                                            text = cell.trim(),
                                            fontSize = 12.sp,
                                            fontFamily = FontFamily.Monospace,
                                            fontWeight = if (isHeader) FontWeight.Bold else FontWeight.Normal,
                                            color = color,
                                            modifier = Modifier
                                                .padding(horizontal = 8.dp, vertical = 2.dp)
                                                .widthIn(min = 60.dp),
                                            maxLines = 1,
                                            lineHeight = 16.sp
                                        )
                                        if (colIndex < row.size - 1) {
                                            Text(
                                                text = "│",
                                                fontSize = 12.sp,
                                                fontFamily = FontFamily.Monospace,
                                                color = color.copy(alpha = 0.4f)
                                            )
                                        }
                                    }
                                }
                                if (rowIndex == 0 && block.rows.size > 1) {
                                    Spacer(
                                        modifier = Modifier
                                            .height(1.dp)
                                            .fillMaxWidth()
                                            .background(color.copy(alpha = 0.2f))
                                    )
                                }
                            }
                        }
                    }
                    Spacer(Modifier.height(6.dp))
                }
                is MarkdownBlock.Heading -> {
                    val hSize = when (block.level) {
                        1 -> H1_SIZE; 2 -> H2_SIZE; 3 -> H3_SIZE
                        4 -> H4_SIZE; 5 -> H5_SIZE; else -> H6_SIZE
                    }
                    Text(
                        text = buildAnnotatedString {
                            withStyle(SpanStyle(fontSize = hSize.sp, fontWeight = FontWeight.Bold)) {
                                appendInlineFormatted(block.text, color, Color(0xFF6C63FF))
                            }
                        },
                        color = color,
                        lineHeight = 20.sp
                    )
                }
                is MarkdownBlock.Blockquote -> {
                    Text(
                        text = block.text,
                        color = color,
                        lineHeight = 20.sp,
                        fontStyle = FontStyle.Italic,
                        modifier = Modifier.padding(start = 12.dp)
                    )
                }
                is MarkdownBlock.UnorderedListItem -> {
                    Text(
                        text = block.text,
                        color = color,
                        lineHeight = 20.sp
                    )
                }
                is MarkdownBlock.HorizontalRule -> {
                    Spacer(Modifier.height(4.dp))
                    Text(
                        text = HR_CHAR.repeat(30),
                        color = color.copy(alpha = 0.3f),
                        fontSize = 10.sp
                    )
                    Spacer(Modifier.height(4.dp))
                }
            }
        }
    }
}

private fun parseBlocks(markdown: String, baseColor: Color): List<MarkdownBlock> {
    val lines = markdown.split("\n")
    val blocks = mutableListOf<MarkdownBlock>()
    val linkColor = Color(0xFF6C63FF)
    var i = 0

    while (i < lines.size) {
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
            blocks.add(MarkdownBlock.CodeBlock(sb.toString(), lang))
            if (i < lines.size) i++
            continue
        }

        // Table detection
        if (line.trimStart().startsWith("|") && line.contains("|")) {
            val tableRows = mutableListOf<List<String>>()
            val cells = line.trim().split("|").filter { it.isNotBlank() }.map { it.trim() }
            tableRows.add(cells)

            i++
            // Check for separator row
            if (i < lines.size) {
                val sepLine = lines[i].trim()
                if (sepLine.startsWith("|") && sepLine.contains("---")) {
                    i++
                    while (i < lines.size && lines[i].trimStart().startsWith("|") && lines[i].contains("|")) {
                        val rowCells = lines[i].trim().split("|").filter { it.isNotBlank() }.map { it.trim() }
                        tableRows.add(rowCells)
                        i++
                    }
                }
            }
            blocks.add(MarkdownBlock.Table(tableRows))
            continue
        }

        // Horizontal rule
        if (line.matches(Regex("^\\s*[-*_]{3,}\\s*$"))) {
            blocks.add(MarkdownBlock.HorizontalRule)
            i++
            continue
        }

        // Blockquote
        val bqMatch = Regex("^ {0,3}>{1,3}\\s+(.*)").find(line)
        if (bqMatch != null) {
            val quoteText = bqMatch.groupValues[1]
            val annotated = buildAnnotatedString {
                withStyle(SpanStyle(color = baseColor.copy(alpha = 0.7f), fontStyle = FontStyle.Italic)) {
                    append("\u275D ")
                }
                appendInlineFormatted(quoteText, baseColor, linkColor)
            }
            blocks.add(MarkdownBlock.Blockquote(annotated))
            i++
            continue
        }

        // Headers
        val hMatch = Regex("^(#{1,6})\\s+(.*)").find(line)
        if (hMatch != null) {
            val level = hMatch.groupValues[1].length
            val headerText = hMatch.groupValues[2].trim()
            blocks.add(MarkdownBlock.Heading(level, headerText))
            i++
            continue
        }

        // Unordered list
        val ulMatch = Regex("^\\s*[-*+]\\s+(.*)").find(line)
        if (ulMatch != null) {
            val itemText = ulMatch.groupValues[1]
            val annotated = buildAnnotatedString {
                withStyle(SpanStyle(fontWeight = FontWeight.Normal)) {
                    append(BULLET)
                }
                appendInlineFormatted(itemText, baseColor, linkColor)
            }
            blocks.add(MarkdownBlock.UnorderedListItem(annotated))
            i++
            continue
        }

        val olMatch = Regex("^\\s*\\d+\\.\\s+(.*)").find(line)
        if (olMatch != null) {
            val itemText = olMatch.groupValues[1]
            val annotated = buildAnnotatedString {
                appendInlineFormatted(itemText, baseColor, linkColor)
            }
            blocks.add(MarkdownBlock.UnorderedListItem(annotated))
            i++
            continue
        }

        // Regular paragraph line
        if (line.isNotBlank() || (i + 1 < lines.size && lines[i + 1].isNotBlank())) {
            val para = buildAnnotatedString {
                appendInlineFormatted(line, baseColor, linkColor)
            }
            blocks.add(MarkdownBlock.Paragraph(para))
        }
        i++
    }

    return blocks
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
