"""
SQLite storage for the filesystem graph.

Based on DESIGN.md Section 3.2 / Step 0.4.
"""
from __future__ import annotations

import sqlite3
from typing import Optional

import numpy as np

from hgnfs_py.graph.schema import (
    NodeType, EdgeType, GraphNode, GraphEdge, FilesystemGraph,
)

DIM = 16  # spatial dimension


def create_tables(conn: sqlite3.Connection) -> None:
    z_cols = ", ".join(f"z_{i} REAL NOT NULL DEFAULT 0.0" for i in range(DIM))
    conn.executescript(f"""
        CREATE TABLE IF NOT EXISTS graph_nodes (
            node_id   INTEGER PRIMARY KEY,
            node_type TEXT NOT NULL,
            path      TEXT UNIQUE NOT NULL,
            name      TEXT NOT NULL,
            ext       TEXT DEFAULT '',
            size      INTEGER DEFAULT 0,
            mtime     REAL DEFAULT 0.0,
            {z_cols}
        );
        CREATE INDEX IF NOT EXISTS idx_node_type ON graph_nodes(node_type);
        CREATE INDEX IF NOT EXISTS idx_node_path ON graph_nodes(path);

        CREATE TABLE IF NOT EXISTS graph_edges (
            source_id  INTEGER NOT NULL,
            target_id  INTEGER NOT NULL,
            edge_type  TEXT NOT NULL,
            weight     REAL DEFAULT 1.0,
            curvature  REAL,
            UNIQUE(source_id, target_id, edge_type),
            FOREIGN KEY (source_id) REFERENCES graph_nodes(node_id) ON DELETE CASCADE,
            FOREIGN KEY (target_id) REFERENCES graph_nodes(node_id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_edge_src ON graph_edges(source_id);
        CREATE INDEX IF NOT EXISTS idx_edge_tgt ON graph_edges(target_id);
    """)


def connect(db_path: str) -> sqlite3.Connection:
    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA foreign_keys=ON")
    create_tables(conn)
    return conn


def insert_graph(
    conn: sqlite3.Connection, graph: FilesystemGraph,
) -> None:
    """Insert all nodes and edges from a FilesystemGraph into the database."""
    z_placeholders = ", ".join("?" for _ in range(DIM))
    z_cols = ", ".join(f"z_{i}" for i in range(DIM))

    with conn:
        for node in graph.nodes:
            z = node.z if node.z is not None else [0.0] * DIM
            conn.execute(
                f"INSERT OR REPLACE INTO graph_nodes "
                f"(node_id, node_type, path, name, ext, size, mtime, {z_cols}) "
                f"VALUES (?, ?, ?, ?, ?, ?, ?, {z_placeholders})",
                [node.id, node.type.value, node.path, node.name,
                 node.ext, node.size, node.mtime, *z],
            )
        for edge in graph.edges:
            conn.execute(
                "INSERT OR IGNORE INTO graph_edges "
                "(source_id, target_id, edge_type, weight, curvature) "
                "VALUES (?, ?, ?, ?, ?)",
                [edge.source_id, edge.target_id, edge.type.value,
                 edge.weight, edge.curvature],
            )


def load_all_z(conn: sqlite3.Connection) -> np.ndarray:
    """Load all Euclidean parametrizations from the database."""
    z_cols = ", ".join(f"z_{i}" for i in range(DIM))
    rows = conn.execute(f"SELECT node_id, {z_cols} FROM graph_nodes ORDER BY node_id").fetchall()
    Z = np.zeros((len(rows), DIM), dtype=np.float32)
    for row in rows:
        idx = row[0]
        Z[idx] = np.array(row[1:], dtype=np.float32)
    return Z


def load_graph(conn: sqlite3.Connection) -> FilesystemGraph:
    """Reconstruct a FilesystemGraph from the database."""
    graph = FilesystemGraph()
    z_cols = ", ".join(f"z_{i}" for i in range(DIM))
    for row in conn.execute(f"SELECT node_id, node_type, path, name, ext, size, mtime, {z_cols} "
                            f"FROM graph_nodes ORDER BY node_id"):
        z = list(row[7:]) if any(v != 0.0 for v in row[7:]) else None
        node = GraphNode(
            id=row[0], type=NodeType(row[1]),
            path=row[2], name=row[3], ext=row[4] or "",
            size=row[5], mtime=row[6], z=z,
        )
        graph.nodes.append(node)
    for row in conn.execute("SELECT source_id, target_id, edge_type, weight, curvature "
                            "FROM graph_edges ORDER BY source_id, target_id"):
        graph.edges.append(GraphEdge(
            source_id=row[0], target_id=row[1],
            type=EdgeType(row[2]), weight=row[3], curvature=row[4],
        ))
    return graph
