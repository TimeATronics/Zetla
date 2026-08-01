"""
Filesystem graph schema - node and edge type definitions.

Based on DESIGN.md Section 4 (Graph Neural Architecture).
"""
from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Optional


class NodeType(str, Enum):
    DIRECTORY = "DIR"
    FILE = "FILE"
    SYMLINK = "SYMLINK"


class EdgeType(str, Enum):
    CONTAINS = "CONTAINS"          # Dir -> File or Dir -> Dir
    SYMLINK_TO = "SYMLINK_TO"     # Symlink -> Target
    CO_ACCESSED = "CO_ACCESSED"    # Files opened together
    CONTENT_SIMILAR = "CONTENT_SIMILAR"  # High similarity


@dataclass
class GraphNode:
    """A node in the filesystem hyperbolic graph."""

    id: int
    type: NodeType
    path: str
    name: str
    ext: str = ""
    size: int = 0
    mtime: float = 0.0
    # Euclidean parametrization z ∈ R^d (stored; converted to Lorentz on demand)
    z: Optional[list[float]] = None

    @property
    def is_dir(self) -> bool:
        return self.type == NodeType.DIRECTORY

    @property
    def is_file(self) -> bool:
        return self.type == NodeType.FILE


@dataclass
class GraphEdge:
    """A directed edge between two graph nodes."""

    source_id: int
    target_id: int
    type: EdgeType
    weight: float = 1.0
    curvature: Optional[float] = None  # Ollivier-Ricci curvature


@dataclass
class FilesystemGraph:
    """Complete in-memory representation of the filesystem graph."""

    nodes: list[GraphNode] = field(default_factory=list)
    edges: list[GraphEdge] = field(default_factory=list)

    @property
    def n_nodes(self) -> int:
        return len(self.nodes)

    @property
    def n_edges(self) -> int:
        return len(self.edges)

    def get_node_by_path(self, path: str) -> Optional[GraphNode]:
        for n in self.nodes:
            if n.path == path:
                return n
        return None

    def get_children(self, node_id: int) -> list[int]:
        return [
            e.target_id
            for e in self.edges
            if e.source_id == node_id and e.type == EdgeType.CONTAINS
        ]

    def get_parent(self, node_id: int) -> Optional[int]:
        for e in self.edges:
            if e.target_id == node_id and e.type == EdgeType.CONTAINS:
                return e.source_id
        return None
