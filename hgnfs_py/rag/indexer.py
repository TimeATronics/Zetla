"""
LorentzRAGIndex - in-memory hyperbolic chunk index for RAG.

Based on DESIGN.md APPENDIX A, Sections A.3-A.11.
"""
from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Optional

import numpy as np
import torch


@dataclass
class RAGResult:
    path: str
    chunk_idx: int
    chunk_text: str
    score: float       # Lorentz inner product (higher = closer)
    start_char: int = 0
    end_char: int = 0


@dataclass
class ChunkMeta:
    path: str
    chunk_idx: int
    start_char: int = 0
    end_char: int = 0
    chunk_type: str = "text"


class LorentzRAGIndex:
    """
    In-memory hyperbolic retrieval index for document chunks.

    Stores chunks as Euclidean parametrizations Z ∈ R^{N×d} (compact),
    converts to Lorentz points on demand for sub-millisecond search.

    Supports:
      - Incremental insertion (no retraining)
      - Hierarchical file scoping
      - Batch indexing from filesystem graph coordinates
    """

    def __init__(self, dim: int = 16):
        self.dim = dim
        self.z_list: list[np.ndarray] = []       # Euclidean params
        self.meta_list: list[ChunkMeta] = []
        self.file_coords: dict[str, np.ndarray] = {}  # path -> Lorentz point (d+1,)
        self._chunk_cache: dict[str, list[str]] = {}  # path -> chunks (for retrieval)

    #  indexing 

    def index_file(
        self,
        path: str,
        text: str,
        file_lorentz_point: Optional[np.ndarray] = None,
        chunk_words: int = 500,
        overlap_words: int = 100,
        embed_fn: Optional[callable] = None,
        placement_strength: float = 0.3,
    ) -> int:
        """
        Index a text file: chunk -> embed -> project -> store.

        Args:
            path: file path (for metadata).
            text: file content.
            file_lorentz_point: (d+1,) Lorentz coordinate of this file (from filesystem GNN).
            chunk_words, overlap_words: text chunking parameters.
            embed_fn: callable that takes list[str] -> np.ndarray (N, d) for batched embedding.
            placement_strength: how close to place chunks to their parent file.

        Returns:
            Number of chunks indexed.
        """
        from hgnfs_py.core.lorentz import exp_o, log_o, exp_x, parallel_transport_o_to_x, _project
        from hgnfs_py.rag.chunker import chunk_text

        chunks = chunk_text(text, chunk_words, overlap_words)
        if not chunks:
            return 0

        self._chunk_cache[path] = chunks

        # Batch embed all chunks at once
        if embed_fn is not None:
            all_z = embed_fn(chunks)  # (N, d)
        else:
            all_z = np.array([_random_embedding(c, self.dim) for c in chunks], dtype=np.float32)

        for i in range(len(chunks)):
            z = all_z[i, :self.dim].astype(np.float32)
            z_t = torch.from_numpy(z).unsqueeze(0)
            x_raw = _project(exp_o(torch.nn.functional.pad(z_t, (1, 0), value=0.0))).squeeze(0)

            # Inductive placement near parent file
            if file_lorentz_point is not None:
                x_parent = torch.from_numpy(file_lorentz_point)
                v = log_o(x_raw.unsqueeze(0))
                v_local = parallel_transport_o_to_x(v, x_parent.unsqueeze(0)).squeeze(0)
                x_chunk = exp_x(
                    placement_strength * v_local.unsqueeze(0),
                    x_parent.unsqueeze(0),
                ).squeeze(0)
                z_chunk = log_o(x_chunk.unsqueeze(0)).squeeze(0)[1:].numpy()
            else:
                z_chunk = z

            self.z_list.append(z_chunk)
            self.meta_list.append(ChunkMeta(path=path, chunk_idx=i))

        if file_lorentz_point is not None:
            self.file_coords[path] = file_lorentz_point
        return len(chunks)

    def add_chunk(
        self,
        path: str,
        text: str,
        chunk_idx: int,
        embed_fn: Optional[callable] = None,
    ) -> None:
        """Incremental add - zero retraining."""
        if embed_fn is not None:
            euc = embed_fn([text])[0]
        else:
            euc = _random_embedding(text, self.dim)
        z = euc[:self.dim].astype(np.float32)
        z_t = torch.from_numpy(z).unsqueeze(0)
        from hgnfs_py.core.lorentz import exp_o, log_o, _project
        x_raw = _project(exp_o(torch.nn.functional.pad(z_t, (1, 0), value=0.0))).squeeze(0)

        x_parent_t = self.file_coords.get(path)
        if x_parent_t is not None:
            from hgnfs_py.core.lorentz import exp_x, parallel_transport_o_to_x, log_o
            x_parent = torch.from_numpy(x_parent_t)
            v = log_o(x_raw.unsqueeze(0))
            v_local = parallel_transport_o_to_x(v, x_parent.unsqueeze(0)).squeeze(0)
            x_chunk = exp_x(0.3 * v_local.unsqueeze(0), x_parent.unsqueeze(0)).squeeze(0)
            z_chunk = log_o(x_chunk.unsqueeze(0)).squeeze(0)[1:].numpy()
        else:
            z_chunk = z

        self.z_list.append(z_chunk)
        self.meta_list.append(ChunkMeta(path=path, chunk_idx=chunk_idx))
        if path in self._chunk_cache:
            self._chunk_cache[path].append(text)

    #  retrieval 

    def search(
        self,
        query: str,
        top_k: int = 10,
        scope_path: Optional[str] = None,
        embed_fn: Optional[callable] = None,
    ) -> list[RAGResult]:
        """
        Search the chunk index for the top-K most relevant chunks.

        Args:
            query: natural language query string.
            top_k: number of results.
            scope_path: if set, restrict to chunks from this file only.
            embed_fn: callable list[str] -> np.ndarray (N, d). Same function used for indexing.
            placement_strength: how close to place chunks to their parent file.

        Returns:
            List of RAGResult sorted by relevance.
        """
        if not self.z_list:
            return []

        Z = np.stack(self.z_list)  # (N, d)
        Z_t = torch.from_numpy(Z)

        # Embed query (handles list[str] signature via batch embed)
        if embed_fn is not None:
            q_euc = embed_fn([query])  # batch embed with single element
            q_z = q_euc[0, :self.dim].astype(np.float32)  # take first result
        else:
            q_z = _random_embedding(query, self.dim)[:self.dim].astype(np.float32)
        q_t = torch.from_numpy(q_z).unsqueeze(0)

        # Project to Lorentz
        from hgnfs_py.core.lorentz import exp_o, _project
        q_x = _project(exp_o(torch.nn.functional.pad(q_t, (1, 0), value=0.0))).squeeze(0)
        X = _project(exp_o(torch.nn.functional.pad(Z_t, (1, 0), value=0.0)))

        # Lorentz inner product: negate query time, then dot
        q_neg = q_x.clone()
        q_neg[0] = -q_neg[0]
        scores = (X @ q_neg).numpy()

        # Scope filter
        if scope_path:
            mask = np.array([m.path == scope_path for m in self.meta_list])
            scores[~mask] = -np.inf

        # Top-K
        if top_k >= len(scores):
            top = np.argsort(-scores)
        else:
            top = np.argpartition(-scores, top_k)[:top_k]
            top = top[np.argsort(-scores[top])]

        results = []
        for idx in top:
            if scores[idx] <= -1e10:
                continue
            meta = self.meta_list[idx]
            chunk_text_val = ""
            if meta.path in self._chunk_cache:
                chunks = self._chunk_cache[meta.path]
                if meta.chunk_idx < len(chunks):
                    chunk_text_val = chunks[meta.chunk_idx]
            results.append(RAGResult(
                path=meta.path, chunk_idx=meta.chunk_idx,
                chunk_text=chunk_text_val,
                score=float(scores[idx]),
                start_char=meta.start_char, end_char=meta.end_char,
            ))
            if len(results) >= top_k:
                break
        return results

    #  utilities 

    @property
    def n_chunks(self) -> int:
        return len(self.z_list)

    def memory_kb(self) -> float:
        z_bytes = sum(z.nbytes for z in self.z_list)
        meta_bytes = sum(
            len(m.path) + 24 for m in self.meta_list
        )
        return (z_bytes + meta_bytes) / 1024

    def get_file_chunks(self, path: str) -> list[str]:
        return self._chunk_cache.get(path, [])


def _random_embedding(text: str, dim: int) -> np.ndarray:
    """Stable pseudo-random embedding from text hash (no model needed)."""
    h = hash(text) & 0xFFFFFFFF
    rng = np.random.RandomState(h)
    return rng.randn(dim).astype(np.float32) * 0.1
