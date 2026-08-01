"""
Filesystem graph builder - walk a directory tree and construct the node/edge graph.

Based on DESIGN.md Section 4.3-4.4.
"""
from __future__ import annotations

import hashlib
import os
import time
from pathlib import Path
from typing import Optional

import numpy as np
import torch

from hgnfs_py.graph.schema import (
    NodeType, EdgeType, GraphNode, GraphEdge, FilesystemGraph,
)


def _quick_hash(filepath: str, max_bytes: int = 4096) -> str:
    """Hash first `max_bytes` of a file for fast content fingerprinting."""
    try:
        with open(filepath, "rb") as f:
            data = f.read(max_bytes)
        return hashlib.md5(data).hexdigest()
    except OSError:
        return ""


def build_graph(
    root_dir: str,
    include_content_hash: bool = False,
    max_size_mb: float = 50,
    exclude_dirs: Optional[set[str]] = None,
) -> FilesystemGraph:
    """
    Walk `root_dir` and build a FilesystemGraph.

    Args:
        root_dir: path to the root of the filesystem tree.
        include_content_hash: whether to compute content hashes (slower).
        max_size_mb: skip files larger than this (MB).
        exclude_dirs: set of directory names to skip (e.g. {'.venv', '.git', 'node_modules'}).

    Returns:
        A FilesystemGraph with nodes (DIR + FILE) and CONTAINS edges.
    """
    if exclude_dirs is None:
        exclude_dirs = {'.venv', '.git', '__pycache__', 'node_modules', '.pytest_cache'}

    root_path = Path(root_dir).resolve()
    graph = FilesystemGraph()
    path_to_id: dict[str, int] = {}
    node_id = 0

    # Add root directory
    root_node = GraphNode(
        id=node_id,
        type=NodeType.DIRECTORY,
        path=str(root_path),
        name=root_path.name or str(root_path),
        mtime=root_path.stat().st_mtime if root_path.exists() else time.time(),
    )
    graph.nodes.append(root_node)
    path_to_id[str(root_path)] = node_id
    node_id += 1

    for dirpath, dirnames, filenames in os.walk(root_path):
        dirpath_resolved = str(Path(dirpath).resolve())
        parent_id = path_to_id.get(dirpath_resolved)
        if parent_id is None:
            continue

        # Filter excluded directories in-place
        dirnames[:] = [d for d in dirnames if d not in exclude_dirs]

        # Subdirectories
        for dname in sorted(dirnames):
            full = str((Path(dirpath) / dname).resolve())
            try:
                st = os.stat(full)
            except OSError:
                continue
            nd = GraphNode(
                id=node_id, type=NodeType.DIRECTORY,
                path=full, name=dname,
                mtime=st.st_mtime,
            )
            graph.nodes.append(nd)
            path_to_id[full] = node_id
            graph.edges.append(GraphEdge(
                source_id=parent_id, target_id=node_id, type=EdgeType.CONTAINS,
            ))
            node_id += 1

        # Files
        for fname in sorted(filenames):
            full = str((Path(dirpath) / fname).resolve())
            try:
                st = os.stat(full)
            except OSError:
                continue
            size_mb = st.st_size / (1024 * 1024)
            if size_mb > max_size_mb:
                continue
            ext = Path(fname).suffix.lower()
            content_hash = _quick_hash(full) if include_content_hash else ""
            nf = GraphNode(
                id=node_id, type=NodeType.FILE,
                path=full, name=fname, ext=ext,
                size=st.st_size, mtime=st.st_mtime,
            )
            graph.nodes.append(nf)
            path_to_id[full] = node_id
            graph.edges.append(GraphEdge(
                source_id=parent_id, target_id=node_id, type=EdgeType.CONTAINS,
            ))
            node_id += 1

    return graph


def graph_to_adjacency(
    graph: FilesystemGraph, normalize: bool = True
) -> torch.Tensor:
    """
    Convert a FilesystemGraph's edges to a normalized sparse adjacency matrix.

    Args:
        graph: FilesystemGraph.
        normalize: if True, row-normalize (D^{-1/2} A D^{-1/2}) for GCN.

    Returns:
        Sparse coalesced tensor (N, N).
    """
    N = graph.n_nodes
    rows, cols, vals = [], [], []
    for e in graph.edges:
        rows.append(e.source_id)
        cols.append(e.target_id)
        vals.append(e.weight)
        # Undirected: add reverse edge too
        rows.append(e.target_id)
        cols.append(e.source_id)
        vals.append(e.weight)

    indices = torch.tensor([rows, cols], dtype=torch.long)
    values = torch.tensor(vals, dtype=torch.float32)
    adj = torch.sparse_coo_tensor(indices, values, (N, N)).coalesce()

    if normalize:
        deg = torch.sparse.sum(adj, dim=1).to_dense().clamp(min=1)
        deg_inv_sqrt = deg.pow(-0.5)
        D_inv = torch.sparse_coo_tensor(
            torch.stack([torch.arange(N), torch.arange(N)]),
            deg_inv_sqrt, (N, N),
        ).coalesce()
        adj = torch.sparse.mm(torch.sparse.mm(D_inv, adj), D_inv).coalesce()

    return adj


def init_euclidean_params(
    n_nodes: int, dim: int = 16, init_scale: float = 0.01,
) -> np.ndarray:
    """Initialize Euclidean parametrization Z ∈ R^{N×d} for all nodes."""
    return np.random.randn(n_nodes, dim).astype(np.float32) * init_scale
