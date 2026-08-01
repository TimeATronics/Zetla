"""
Hyperbolic Graph Convolutional Networks (HGCN).

Directly based on:
  - refs/hgcn/layers/hyp_layers.py  (HyperbolicGraphConvolution, HypLinear, HypAgg, HypAct)
  - refs/hgcn/models/encoders.py    (HGCN encoder)
  - refs/hgcn/config.py             (default hyperparams)
"""
from __future__ import annotations

import math
from typing import Optional

import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.nn.init as init

from hgnfs_py.gnn.attention import HyperbolicAttention


# 
# HypLinear - feature transformation in Lorentz space
# 

class HypLinear(nn.Module):
    """
    Hyperbolic linear layer: exp₀(W · log₀(x)) + Möbius bias.

    From refs/hgcn/layers/hyp_layers.py:78-109 (HypLinear).
    """

    def __init__(
        self,
        in_features: int,
        out_features: int,
        dropout: float = 0.0,
        use_bias: bool = True,
    ):
        super().__init__()
        self.in_features = in_features
        self.out_features = out_features
        self.dropout = dropout
        self.use_bias = use_bias

        self.weight = nn.Parameter(torch.Tensor(out_features, in_features))
        self.bias = nn.Parameter(torch.Tensor(out_features)) if use_bias else None
        self.reset_parameters()

    def reset_parameters(self):
        init.xavier_uniform_(self.weight, gain=math.sqrt(2))
        if self.bias is not None:
            init.constant_(self.bias, 0)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        from hgnfs_py.core.lorentz import mobius_matvec, exp_o, _project

        w = F.dropout(self.weight, self.dropout, training=self.training)
        mv = mobius_matvec(w, x)         # exp₀(W·log₀(x))
        res = _project(mv)

        if self.use_bias:
            b_padded = F.pad(self.bias.view(1, -1), (1, 0), value=0.0)
            hyp_bias = _project(exp_o(b_padded))
            res = _project(mobius_add(res, hyp_bias))

        return res


def mobius_add(x: torch.Tensor, y: torch.Tensor) -> torch.Tensor:
    """
    Möbius addition via exp_x(log_o): x ⊕ y = exp_x(ptransp0(log_o(y))).

    From refs/hgcn/manifolds/hyperboloid.py:115-118 (mobius_add).
    """
    from hgnfs_py.core.lorentz import log_o, parallel_transport_o_to_x, exp_x

    u = log_o(y)
    v = parallel_transport_o_to_x(u, x)
    return exp_x(v, x)


# 
# HypAgg - neighborhood aggregation
# 

class HypAgg(nn.Module):
    """
    Hyperbolic neighborhood aggregation.

    Two modes:
      - local_agg=False: aggregate in origin tangent space (faster).
      - local_agg=True:  aggregate in each center node's tangent space (better accuracy).
        This is the paper-recommended variant.

    From refs/hgcn/layers/hyp_layers.py:117-153 (HypAgg).
    """

    def __init__(
        self,
        in_features: int,
        dropout: float = 0.0,
        use_att: bool = False,
        local_agg: bool = True,
    ):
        super().__init__()
        self.in_features = in_features  # spatial dim of input
        self.dropout = dropout
        self.use_att = use_att
        self.local_agg = local_agg
        if use_att:
            self.att = HyperbolicAttention(in_features, dropout)

    def forward(
        self, x: torch.Tensor, adj: torch.Tensor
    ) -> torch.Tensor:
        """
        Args:
            x:   (N, d+1) - Lorentz-embedded node features.
            adj: (N, N) sparse - normalized adjacency matrix.

        Returns:
            (N, d+1) - aggregated Lorentz features.
        """
        from hgnfs_py.core.lorentz import log_o, log_x, exp_o, exp_x, _project

        x_tangent = log_o(x)[..., 1:]  # (N, d), spatial only in origin tan space

        if self.use_att and self.local_agg:
            # --- Center-node tangent space aggregation (recommended) ---
            adj_att = self.att(x_tangent, adj)  # (N, N) dense attention
            N = x.size(0)
            agg = torch.zeros_like(x)
            for i in range(N):
                # adj[i] is a 1D sparse tensor (if adj is 2D sparse)
                if adj.is_sparse:
                    row_slice = adj[i]
                    nbr_indices = row_slice._indices()
                    neighbors = nbr_indices.squeeze(0) if nbr_indices.dim() > 1 else nbr_indices
                else:
                    neighbors = adj[i].nonzero(as_tuple=True)[0]
                if neighbors.numel() == 0:
                    agg[i] = x[i]
                    continue
                n_pts = x[neighbors]
                n_tan = log_x(n_pts, x[i:i+1].expand(len(neighbors), -1))
                w = adj_att[i, neighbors].unsqueeze(-1)
                support = (w * n_tan).sum(dim=0)
                agg[i] = _project(exp_x(support.unsqueeze(0), x[i:i+1])).squeeze(0)
            return agg

        elif self.use_att and not self.local_agg:
            # --- Origin tangent space with attention ---
            adj_att = self.att(x_tangent, adj)          # (N, N)
            support_t = adj_att @ x_tangent              # (N, d)
            return _project(exp_o(F.pad(support_t, (1, 0), value=0.0)))

        else:
            # --- Origin tangent space, no attention ---
            if adj.is_sparse:
                support_t = torch.spmm(adj, x_tangent)   # (N, d)
            else:
                support_t = adj @ x_tangent
            return _project(exp_o(F.pad(support_t, (1, 0), value=0.0)))


# 
# HypAct - hyperbolic activation
# 

class HypAct(nn.Module):
    """
    Hyperbolic activation via tangent-space sandwich: act(log₀(x)) -> exp₀.

    From refs/hgcn/layers/hyp_layers.py:159-179 (HypAct).
    """

    def __init__(self, act: str = "relu"):
        super().__init__()
        self.act = getattr(F, act) if act else (lambda x: x)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        from hgnfs_py.core.lorentz import log_o, exp_o, _project

        v = log_o(x)                        # to tangent space
        v_space = self.act(v[..., 1:])       # activation on spatial
        v_act = F.pad(v_space, (1, 0), value=0.0)
        return _project(exp_o(v_act))


# 
# HyperbolicGraphConvolution - full HGCN layer
# 

class HyperbolicGraphConvolution(nn.Module):
    """
    One HGCN layer: HypLinear -> HypAgg -> HypAct.

    From refs/hgcn/layers/hyp_layers.py:58-75 (HyperbolicGraphConvolution).
    """

    def __init__(
        self,
        in_features: int,
        out_features: int,
        dropout: float = 0.0,
        use_bias: bool = True,
        use_att: bool = False,
        local_agg: bool = True,
        act: str = "relu",
    ):
        super().__init__()
        self.linear = HypLinear(in_features, out_features, dropout, use_bias)
        self.agg = HypAgg(out_features, dropout, use_att, local_agg)
        self.hyp_act = HypAct(act)

    def forward(self, x: torch.Tensor, adj: torch.Tensor) -> torch.Tensor:
        h = self.linear(x)
        h = self.agg(h, adj)
        h = self.hyp_act(h)
        return h


# 
# HGCN - full encoder stack
# 

class HGCN(nn.Module):
    """
    Full Hyperbolic Graph Convolutional Network encoder.

    Architecture: input projection -> [HGCNLayer × num_layers] -> output.

    From refs/hgcn/models/encoders.py:93-121 (HGCN encoder).

    Args:
        in_dim:        spatial dimension of input features (before Lorentz lift).
        hidden_dim:    spatial dimension of hidden layers.
        out_dim:       spatial dimension of output embeddings.
        num_layers:    number of HGCN layers (≥1).
        dropout:       DropConnect rate on HypLinear weights.
        use_bias:      whether to add Möbius bias in HypLinear.
        use_att:       whether to use hyperbolic attention in HypAgg.
        local_agg:     whether to aggregate in center-node tangent space.
        act:           activation function name ("relu", "leaky_relu", etc.).
    """

    def __init__(
        self,
        in_dim: int,
        hidden_dim: int = 128,
        out_dim: int = 128,
        num_layers: int = 2,
        dropout: float = 0.0,
        use_bias: bool = True,
        use_att: bool = False,
        local_agg: bool = True,
        act: str = "relu",
    ):
        super().__init__()
        dims = [in_dim] + [hidden_dim] * max(0, num_layers - 1) + [out_dim]
        self.num_layers = num_layers
        self.layers = nn.ModuleList()
        for i in range(num_layers):
            self.layers.append(
                HyperbolicGraphConvolution(
                    in_features=dims[i],
                    out_features=dims[i + 1],
                    dropout=dropout,
                    use_bias=use_bias,
                    use_att=use_att,
                    local_agg=local_agg,
                    act=act if i < num_layers - 1 else "relu",
                )
            )

    def forward(
        self, x: torch.Tensor, adj: torch.Tensor
    ) -> torch.Tensor:
        """
        Args:
            x:   (N, in_dim) - Euclidean input features.
            adj: (N, N) sparse - normalized adjacency matrix.

        Returns:
            h:   (N, out_dim+1) - Lorentz point embeddings.
        """
        from hgnfs_py.core.lorentz import exp_o, _project

        # Lift Euclidean features to Lorentz manifold
        x_padded = F.pad(x, (1, 0), value=0.0)
        h = _project(exp_o(x_padded))

        for layer in self.layers:
            h = layer(h, adj)
        return h
