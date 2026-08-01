"""
Hierarchical scope filtering for CAH-RAG.

Leverages the shared Lorentz geometry between the filesystem graph
and the chunk index for natural hierarchical query scoping.

Based on DESIGN.md APPENDIX A, Section A.6.
"""
from __future__ import annotations

from typing import Optional

import numpy as np
import torch


def hard_scope_mask(
    chunk_paths: list[str],
    scope_paths: list[str],
) -> np.ndarray:
    """
    Boolean mask: True for chunks whose path is in scope_paths.
    """
    scope_set = set(scope_paths)
    return np.array([p in scope_set for p in chunk_paths])


def soft_scope_weights(
    query_point: np.ndarray,
    chunk_points: np.ndarray,
    chunk_paths: list[str],
    file_points: dict[str, np.ndarray],
) -> np.ndarray:
    """
    Soft geometric weights: weight each chunk by proximity to its parent
    file's coordinate in Lorentz space.

    Args:
        query_point: (d+1,) - query in Lorentz space.
        chunk_points: (N, d+1) - all chunk Lorentz points.
        chunk_paths: list of paths per chunk.
        file_points: {path: (d+1,)} - Lorentz coordinate per file.

    Returns:
        (N,) - weight per chunk in [0, 1].
    """
    weights = np.ones(len(chunk_paths), dtype=np.float32)
    for i, path in enumerate(chunk_paths):
        fp = file_points.get(path)
        if fp is not None:
            d = _lorentz_distance(chunk_points[i], fp)
            weights[i] = 1.0 / (1.0 + d)
    return weights


def fuzzy_scope_mask(
    chunk_paths: list[str],
    chunk_points: np.ndarray,
    scope_file_point: np.ndarray,
    max_distance: float = 1.0,
) -> np.ndarray:
    """
    Fuzzy scope: include chunks from specified file AND chunks
    geometrically close (< max_distance) to it.
    """
    mask = np.zeros(len(chunk_paths), dtype=bool)
    for i, (path, pt) in enumerate(zip(chunk_paths, chunk_points)):
        d = _lorentz_distance(pt, scope_file_point)
        mask[i] = d < max_distance
    return mask


def _lorentz_distance(x: np.ndarray, y: np.ndarray) -> float:
    d_inner = -x[0] * y[0] + np.dot(x[1:], y[1:])
    return float(np.arccosh(max(-d_inner, 1.0 + 1e-12)))
