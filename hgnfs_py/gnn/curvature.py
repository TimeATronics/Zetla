"""
Ollivier-Ricci curvature computation for graph edges.

Based on κHGCN (Yang et al., KDD 2023):
  - ORC κ_ij = 1 - W(m_i, m_j) / d(i,j)  where W is Wasserstein distance
  - κ ∈ (-2, 1); positive = dense cluster, negative = bridge/tree edge

For efficiency, we use the Jaccard coefficient proxy:
  - κ̂_ij = 2 * |N(i) ∩ N(j)| / |N(i) ∪ N(j)| - 1
  - κ̂ ∈ [-1, 1]; 1 = identical neighborhoods, -1 = completely disjoint
"""

from __future__ import annotations

import torch
import numpy as np
from collections.abc import Sequence


def jaccard_curvature(
    adj_list: Sequence[Sequence[int]], edges: torch.Tensor
) -> torch.Tensor:
    """
    Jaccard-based proxy for Ollivier-Ricci curvature on edges.

    Args:
        adj_list: list of neighbor index lists, one per node.
        edges:    (E, 2) - int64 tensor of edge index pairs.

    Returns:
        (E,) - curvature values in [-1, 1].
    """
    curvatures = torch.zeros(edges.size(0), dtype=torch.float32)
    for k, (i, j) in enumerate(edges.tolist()):
        ni = set(adj_list[i])
        nj = set(adj_list[j])
        ni.discard(i)  # remove self-loops
        nj.discard(j)
        if not ni and not nj:
            curvatures[k] = 0.0
            continue
        intersection = len(ni & nj)
        union = len(ni | nj)
        if union == 0:
            curvatures[k] = 0.0
        else:
            curvatures[k] = 2.0 * intersection / union - 1.0
    return curvatures


def adjacency_to_list(adj: torch.Tensor) -> list[set[int]]:
    """Convert sparse adj (N,N) to list-of-sets format."""
    N = adj.size(0)
    adj_list: list[set[int]] = [set() for _ in range(N)]
    if adj.is_sparse:
        indices = adj._indices()
        for r, c in indices.t().tolist():
            adj_list[r].add(c)
    else:
        adj_dense = adj if not adj.is_sparse else adj.to_dense()
        for i in range(N):
            adj_list[i] = set(adj_dense[i].nonzero(as_tuple=True)[1].tolist())
    return adj_list
