"""
Graph decoders - map hyperbolic embeddings to predictions.

Based on:
  - refs/hgcn/models/decoders.py     (LinearDecoder)
  - refs/hgcn/layers/layers.py       (FermiDiracDecoder)
  - refs/hgcn/models/base_models.py  (LPModel, NCModel)
"""
import torch
import torch.nn as nn
import torch.nn.functional as F


# 
# Fermi-Dirac Decoder - link prediction
# 

class FermiDiracDecoder(nn.Module):
    """
    Edge probability from hyperbolic distance: p = 1/(exp((d² - r)/t) + 1).

    From refs/hgcn/layers/layers.py:77-87.

    Args:
        r: connection radius (smaller = fewer edges predicted).
        t: temperature / steepness.
    """

    def __init__(self, r: float = 2.0, t: float = 1.0):
        super().__init__()
        self.r = r
        self.t = t

    def forward(self, sqdist: torch.Tensor) -> torch.Tensor:
        """
        Args:
            sqdist: (*,) - squared hyperbolic distance.
        Returns:
            probs:  (*,) - edge probability in (0, 1).
        """
        return torch.sigmoid((self.r - sqdist) / self.t)


class LinkPredictionModel(nn.Module):
    """
    Full link prediction pipeline: HGCN encoder + Fermi-Dirac decoder.

    From refs/hgcn/models/base_models.py:92-135 (LPModel).
    """

    def __init__(
        self,
        encoder: nn.Module,
        r: float = 2.0,
        t: float = 1.0,
    ):
        super().__init__()
        self.encoder = encoder
        self.decoder = FermiDiracDecoder(r, t)

    def encode(self, x: torch.Tensor, adj: torch.Tensor) -> torch.Tensor:
        return self.encoder(x, adj)

    def decode(self, h: torch.Tensor, edges: torch.Tensor) -> torch.Tensor:
        """
        Args:
            h:     (N, d+1) - Lorentz embeddings.
            edges: (E, 2)  - int64 index pairs.
        Returns:
            probs: (E,)    - edge probabilities.
        """
        from hgnfs_py.core.lorentz import squared_distance

        src, dst = edges[:, 0], edges[:, 1]
        sqdist = squared_distance(h[src], h[dst])
        return self.decoder(sqdist)

    def compute_loss(
        self,
        h: torch.Tensor,
        pos_edges: torch.Tensor,
        neg_edges: torch.Tensor,
    ) -> torch.Tensor:
        """Binary cross-entropy loss on positive + negative edges."""
        pos_scores = self.decode(h, pos_edges)
        neg_scores = self.decode(h, neg_edges)
        pos_loss = F.binary_cross_entropy(pos_scores, torch.ones_like(pos_scores))
        neg_loss = F.binary_cross_entropy(neg_scores, torch.zeros_like(neg_scores))
        return pos_loss + neg_loss


# 
# Classification Decoder
# 

class ClassificationDecoder(nn.Module):
    """
    Node classification for hyperbolic embeddings.
    Maps to tangent space at origin, then applies a linear classifier + log-softmax.

    From refs/hgcn/models/decoders.py:51-72 (LinearDecoder).
    """

    def __init__(self, in_dim: int, n_classes: int, dropout: float = 0.0):
        super().__init__()
        self.linear = nn.Linear(in_dim, n_classes)
        self.dropout = nn.Dropout(dropout)

    def forward(self, h: torch.Tensor) -> torch.Tensor:
        """
        Args:
            h: (N, d+1) - Lorentz embeddings.
        Returns:
            (N, n_classes) - log-softmax class probabilities.
        """
        from hgnfs_py.core.lorentz import log_o
        v = log_o(h)                     # (N, d+1)
        v_space = v[..., 1:]             # (N, d), time=0
        v_space = self.dropout(v_space)
        return F.log_softmax(self.linear(v_space), dim=-1)


class NodeClassificationModel(nn.Module):
    """
    Full node classification pipeline: HGCN encoder + classification decoder.

    From refs/hgcn/models/base_models.py:54-89 (NCModel).
    """

    def __init__(
        self,
        encoder: nn.Module,
        n_classes: int,
        dropout: float = 0.0,
    ):
        super().__init__()
        self.encoder = encoder
        # Output dim = encoder's out_dim (spatial)
        out_dim = encoder.layers[-1].linear.out_features if hasattr(encoder, 'layers') else 128
        self.decoder = ClassificationDecoder(out_dim, n_classes, dropout)

    def encode(self, x: torch.Tensor, adj: torch.Tensor) -> torch.Tensor:
        return self.encoder(x, adj)

    def decode(self, h: torch.Tensor, idx: torch.Tensor | None = None) -> torch.Tensor:
        logits = self.decoder(h)
        if idx is not None:
            logits = logits[idx]
        return logits

    def compute_loss(
        self,
        h: torch.Tensor,
        labels: torch.Tensor,
        idx: torch.Tensor | None = None,
        class_weights: torch.Tensor | None = None,
    ) -> torch.Tensor:
        logits = self.decode(h, idx)
        if idx is not None:
            labels = labels[idx]
        return F.nll_loss(logits, labels, weight=class_weights)
