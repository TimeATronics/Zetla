"""
Tangent space operations for hyperbolic neural networks.
All operations are standard Euclidean ops applied to tangent vectors,
following the Euclidean parametrization approach.
"""

import torch
import torch.nn as nn
import torch.nn.init as init
import torch.nn.functional as F


def tangent_linear(
    v: torch.Tensor, W: torch.Tensor, b: torch.Tensor | None = None
) -> torch.Tensor:
    """
    Linear transform in tangent space.
    v: (*, in_dim) - tangent vector (spatial components only, NOT the full d+1 Lorentz point).
    W: (out_dim, in_dim) - weight matrix.
    b: (out_dim,) or None - bias.
    """
    return F.linear(v, W, b)


def tangent_leaky_relu(v: torch.Tensor, neg_slope: float = 0.5) -> torch.Tensor:
    """LeakyReLU activation in tangent space."""
    return F.leaky_relu(v, neg_slope)


class EuclideanParams(nn.Module):
    """
    Maintains a learnable matrix of Euclidean-parametrized node embeddings Z ∈ R^{N × d}.

    These are optimized with standard Adam - the exp_o map converts to Lorentz
    points only when needed for distance/aggregation computations.
    """

    def __init__(self, n_nodes: int, dim: int, init_scale: float = 0.01):
        super().__init__()
        self.n_nodes = n_nodes
        self.dim = dim
        self.Z = nn.Parameter(torch.randn(n_nodes, dim) * init_scale)

    def forward(self) -> torch.Tensor:
        return self.Z

    def get_lorentz(self) -> torch.Tensor:
        """Convert all Euclidean params to Lorentz points."""
        from hgnfs_py.core.lorentz import exp_o
        return exp_o(F.pad(self.Z, (1, 0), value=0.0))


class LorentzLinear(nn.Module):
    """
    Hyperbolic linear layer: exp_o(W · log_o(x)).
    Weights are Euclidean. Forward takes Lorentz input, returns Lorentz output.
    """

    def __init__(self, in_features: int, out_features: int, dropout: float = 0.0):
        super().__init__()
        self.in_features = in_features  # spatial dim of input (= d for L^d)
        self.out_features = out_features
        self.dropout = dropout
        self.weight = nn.Parameter(torch.Tensor(out_features, in_features))
        self.reset_parameters()

    def reset_parameters(self):
        init.xavier_uniform_(self.weight, gain=init.calculate_gain("relu"))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """
        x: (*, in_features+1) - Lorentz point input.
        Returns: (*, out_features+1) - Lorentz point output.
        """
        from hgnfs_py.core.lorentz import log_o, exp_o

        w = F.dropout(self.weight, self.dropout, training=self.training)
        v = log_o(x)               # (*, in_features+1)
        v_space = v[..., 1:]       # (*, in_features)
        z = F.linear(v_space, w)   # (*, out_features)
        return exp_o(F.pad(z, (1, 0), value=0.0))
