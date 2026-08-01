"""
Text chunking utilities for CAH-RAG (Context-Aware Hyperbolic RAG).

Based on DESIGN.md APPENDIX A Section A.4.
"""
from __future__ import annotations

from typing import Optional


def chunk_text(
    text: str,
    chunk_words: int = 500,
    overlap_words: int = 100,
) -> list[str]:
    """
    Split text into overlapping word-level chunks.

    Args:
        text: input text to chunk.
        chunk_words: target words per chunk.
        overlap_words: overlap between consecutive chunks.

    Returns:
        List of chunk strings.
    """
    if not text.strip():
        return []
    words = text.split()
    if len(words) <= chunk_words:
        return [text]
    chunks = []
    step = max(1, chunk_words - overlap_words)
    for i in range(0, len(words), step):
        chunk = " ".join(words[i : i + chunk_words])
        if chunk.strip():
            chunks.append(chunk)
    return chunks


def chunk_text_sentences(
    text: str,
    max_sentences: int = 20,
) -> list[str]:
    """
    Split text into sentence-group chunks (coarser, faster).

    Args:
        text: input text.
        max_sentences: max sentences per chunk.

    Returns:
        List of chunk strings.
    """
    import re

    sentences = re.split(r'(?<=[.!?])\s+', text)
    chunks = []
    for i in range(0, len(sentences), max_sentences):
        chunk = " ".join(sentences[i : i + max_sentences])
        if chunk.strip():
            chunks.append(chunk)
    return chunks


def read_file_text(filepath: str, max_chars: Optional[int] = None) -> str:
    """
    Read text from a file, attempting UTF-8 with fallback.

    Args:
        filepath: path to the file.
        max_chars: if set, read only the first `max_chars` characters.

    Returns:
        File content as string.
    """
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            if max_chars:
                return f.read(max_chars)
            return f.read()
    except UnicodeDecodeError:
        # Binary file - skip
        return ""
