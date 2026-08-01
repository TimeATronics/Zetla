"""
Hyperbolic attention layers.

Based on HGCN reference: refs/hgcn/layers/att_layers.py (DenseAtt)
and LResNet reference: refs/LResNet/homophilous/layers/hyp_layers.py (Lorentz attention).
"""
import torch
import torch.nn as nn
import torch.nn.functional as F


class HyperbolicAttention(nn.Module):
    """
    Dense pair-wise attention for hyperbolic node features.
    Computes attention weights in the origin tangent space, then masks by adjacency.

    Based on HGCN's DenseAtt (refs/hgcn/layers/att_layers.py:8-28).
    """

    def __init__(self, in_features: int, dropout: float = 0.0):
        super().__init__()
        self.dropout = dropout
        self.linear = nn.Linear(2 * in_features, 1, bias=True)
        self.in_features = in_features

    def forward(
        self, x_tangent: torch.Tensor, adj: torch.Tensor
    ) -> torch.Tensor:
        """
        Args:
            x_tangent: (N, d) - node features in origin tangent space.
            adj:        (N, N) sparse - adjacency matrix.

        Returns:
            att_adj: (N, N) dense - attention weights, masked by adj.
        """
        N = x_tangent.size(0)
        # (N, 1, d) and (1, N, d) - all pair-wise concatenations
        x_left = x_tangent.unsqueeze(1).expand(-1, N, -1)
        x_right = x_tangent.unsqueeze(0).expand(N, -1, -1)
        x_cat = torch.cat([x_left, x_right], dim=-1)  # (N, N, 2d)

        # Pair-wise scores -> sigmoid -> mask by adjacency
        scores = self.linear(x_cat).squeeze(-1)        # (N, N)
        att = torch.sigmoid(scores)
        if adj.is_sparse:
            att = att * adj.to_dense()
        else:
            att = att * adj
        return att


class LorentzAttention(nn.Module):
    """
    Lorentz-space attention using squared distance as similarity signal.

    Based on LResNet's attention in LorentzAgg (refs/LResNet/.../hyp_layers.py:118-136).
    Uses: att_ij = sigmoid(2 + 2·c_inner(q_i, k_j)) = sigmoid(2c - 2⟨q_i,k_j⟩_L) = sigmoid(d²_L).
    """

    def __init__(self, in_features: int, scale: float = 1.0):
        super().__init__()
        self.in_features = in_features   # total dim = d+1 (includes time)
        self.scale = scale
        self.query = nn.Linear(in_features, in_features, bias=False)
        self.key = nn.Linear(in_features, in_features, bias=False)

    def forward(self, x: torch.Tensor, adj: torch.Tensor) -> torch.Tensor:
        """
        Args:
            x:   (N, d+1) - node features in Lorentz space.
            adj: (N, N) - adjacency matrix.

        Returns:
            att_adj: (N, N) - attention weights.
        """
        from hgnfs_py.core.lorentz import lorentz_inner

        q = self.query(x)[..., 1:]    # spatial only
        k = self.key(x)[..., 1:]
        # ⟨q, k⟩_L = -q₀k₀ + Σqᵢkᵢ = Σqᵢkᵢ (time=0 for query/key projections)
        # So -⟨q, k⟩_L = -Σqᵢkᵢ = -(q @ k.T)
        # squared distance proxy: 2 - 2⟨q,k⟩ used in LResNet
        neg_inner = -(q @ k.T)                      # (N, N)
        att_logits = 2.0 + 2.0 * neg_inner          # (N, N)
        att_logits = att_logits / self.scale
        att = torch.sigmoid(att_logits)

        adj_dense = adj.to_dense() if adj.is_sparse else adj
        return att * adj_dense
