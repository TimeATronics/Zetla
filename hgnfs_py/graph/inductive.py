"""
Inductive coordinate placement - place new files without retraining.

Based on DESIGN.md Section 4.4: x_new = exp_x(parent, P_o->x(content_vec)).
"""
from __future__ import annotations

import numpy as np
import torch

from hgnfs_py.graph.schema import FilesystemGraph, GraphNode, NodeType, EdgeType, GraphEdge


def place_new_file(
    graph: FilesystemGraph,
    parent_id: int,
    file_name: str,
    file_path: str,
    content_vec: np.ndarray,
    dim: int = 16,
    placement_strength: float = 0.3,
) -> GraphNode:
    """
    Inductively place a new file node without retraining.

    Algorithm:
        1. Get parent's Euclidean param z_parent -> Lorentz x_parent
        2. Project content_vec to tangent space: v_content = (0, content_vec)
        3. Transport to parent: v_local = parallel_transport_o_to_x(v_content, x_parent)
        4. Place: x_file = exp_x(placement_strength * v_local, x_parent)
        5. Store: z_file = log_o(x_file)

    Args:
        graph: the existing filesystem graph.
        parent_id: node ID of the parent directory.
        file_name: name of the new file.
        file_path: full path of the new file.
        content_vec: content embedding vector ∈ R^k (any dim).
        dim: spatial dimension of the Lorentz manifold.
        placement_strength: how far from parent to place (0.3 = close).

    Returns:
        GraphNode for the new file with z coordinate populated.
    """
    from hgnfs_py.core.lorentz import (
        exp_o, log_o, exp_x, parallel_transport_o_to_x, _project,
    )

    # Parent coordinate
    parent_node = graph.nodes[parent_id]
    if parent_node.z is None:
        raise ValueError(f"Parent node {parent_id} has no Euclidean parametrization")

    z_parent = torch.tensor(parent_node.z, dtype=torch.float32)
    x_parent = _project(exp_o(torch.nn.functional.pad(z_parent.unsqueeze(0), (1, 0), value=0.0))).squeeze(0)

    # Project content vector to manifold
    c = torch.tensor(content_vec, dtype=torch.float32)
    # If content_vec has different dim than manifold, project via linear mapping or truncation
    if c.shape[0] != dim:
        # Simple: take first dim elements or pad with zeros
        if c.shape[0] > dim:
            c = c[:dim]
        else:
            c = torch.nn.functional.pad(c, (0, dim - c.shape[0]))

    # Extract text/TF-IDF features as tangent vector at origin
    v_content = torch.nn.functional.pad(c.unsqueeze(0), (1, 0), value=0.0)
    x_content = _project(exp_o(v_content))

    # Transport to parent's tangent space and place nearby
    v_tangent = log_o(x_content)
    v_local = parallel_transport_o_to_x(v_tangent, x_parent.unsqueeze(0)).squeeze(0)
    x_file = exp_x(placement_strength * v_local.unsqueeze(0), x_parent.unsqueeze(0)).squeeze(0)

    # Store as Euclidean parametrization
    z_file = log_o(x_file.unsqueeze(0)).squeeze(0)[1:].numpy()

    # Create node
    new_id = graph.n_nodes
    node = GraphNode(
        id=new_id, type=NodeType.FILE,
        path=file_path, name=file_name,
        z=z_file.tolist(),
    )
    graph.nodes.append(node)
    graph.edges.append(GraphEdge(
        source_id=parent_id, target_id=new_id, type=EdgeType.CONTAINS,
    ))
    return node
