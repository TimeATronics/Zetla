"""
In-memory hyperbolic search index.

Based on DESIGN.md Section 2.3 / Step 0.5 and APPENDIX A Section A.5.
"""
from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Optional

import numpy as np
import torch


@dataclass
class SearchResult:
    node_id: int
    path: str
    name: str
    node_type: str
    score: float      # negative Lorentzian distance (higher = better)
    distance: float    # hyperbolic geodesic distance


class LorentzIndex:
    """
    In-memory index for fast hyperbolic nearest-neighbor search.

    Stores Euclidean parametrizations Z ∈ R^{N×d}, converts to Lorentz
    points X ∈ R^{N×(d+1)} on demand for distance computation.
    """

    def __init__(self, dim: int = 16):
        self.dim = dim
        self.Z: Optional[np.ndarray] = None           # (N, d)
        self.node_ids: Optional[np.ndarray] = None     # (N,)
        self.meta: list[dict] = []                      # path, name, type per row

    def build(self, z: np.ndarray, node_ids: np.ndarray, meta: list[dict]) -> None:
        """Load embeddings and metadata into the index."""
        assert z.ndim == 2 and z.shape[1] == self.dim
        self.Z = z.astype(np.float32)
        self.node_ids = node_ids
        self.meta = meta

    def search(
        self,
        query_z: np.ndarray,
        top_k: int = 20,
        scope_paths: Optional[list[str]] = None,
    ) -> list[SearchResult]:
        """
        Find top-K nearest neighbors by hyperbolic distance.

        Args:
            query_z: (d,) - Euclidean parametrization of the query.
            top_k: number of results.
            scope_paths: if set, only consider nodes with path in this list.

        Returns:
            List of SearchResult, sorted by score descending (distance ascending).
        """
        if self.Z is None:
            return []

        # Convert to Lorentz points in batch (exp_o)
        t0 = time.perf_counter()
        q_x = _euclidean_to_lorentz(torch.from_numpy(query_z).unsqueeze(0)).squeeze(0)
        X = _euclidean_to_lorentz(torch.from_numpy(self.Z))

        # Lorentz inner product for similarity: ⟨q, x⟩ = -q₀x₀ + Σqᵢxᵢ
        # Negate query's time component, then dot = -(-q₀)x₀ + Σqᵢxᵢ = q₀x₀ + Σqᵢxᵢ
        # We want -⟨q,x⟩_L = q₀x₀ - Σqᵢxᵢ
        # Implementation: just compute inner product directly
        q_neg = q_x.clone()
        q_neg[0] = -q_neg[0]   # negate time
        scores = (X @ q_neg).numpy()  # (N,), higher = closer

        # Scope filter
        if scope_paths:
            scope_set = set(scope_paths)
            mask = np.array([m.get("path", "") in scope_set for m in self.meta])
            scores[~mask] = -np.inf

        # Top-K
        if top_k >= len(scores):
            top_indices = np.argsort(-scores)
        else:
            top_indices = np.argpartition(-scores, top_k)[:top_k]
            top_indices = top_indices[np.argsort(-scores[top_indices])]

        results = []
        for idx in top_indices:
            if scores[idx] <= -1e10:
                continue
            d = _lorentz_distance_np(q_x.numpy(), X[idx].numpy())
            results.append(SearchResult(
                node_id=int(self.node_ids[idx]),
                path=self.meta[idx].get("path", f"node_{idx}"),
                name=self.meta[idx].get("name", ""),
                node_type=self.meta[idx].get("type", ""),
                score=float(scores[idx]),
                distance=float(d),
            ))
            if len(results) >= top_k:
                break

        return results

    def memory_kb(self) -> float:
        if self.Z is None:
            return 0.0
        return (self.Z.nbytes + self.meta_nbytes()) / 1024

    def meta_nbytes(self) -> int:
        return sum(len(str(v)) for m in self.meta for v in m.values())


def _euclidean_to_lorentz(z: torch.Tensor) -> torch.Tensor:
    """Batch exp_o: Euclidean params -> Lorentz points."""
    from hgnfs_py.core.lorentz import exp_o
    v = torch.nn.functional.pad(z, (1, 0), value=0.0)
    return exp_o(v)


def _lorentz_distance_np(x: np.ndarray, y: np.ndarray) -> float:
    """Lorentzian geodesic distance for numpy arrays."""
    inner = -x[0] * y[0] + np.dot(x[1:], y[1:])
    arg = max(-inner, 1.0 + 1e-12)
    return float(np.arccosh(arg))
