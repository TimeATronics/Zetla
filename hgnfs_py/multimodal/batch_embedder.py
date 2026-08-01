"""Batch embedder: embed N texts at once, much faster than one-at-a-time."""
from __future__ import annotations

import numpy as np
import torch
from sentence_transformers import SentenceTransformer

from hgnfs_py.core.lorentz import exp_o
from hgnfs_py.multimodal.embedder import HyperbolicProjector


class BatchEmbedder:
    """Batched text -> Lorentz embedding with BGE-Micro."""

    def __init__(
        self,
        model_name: str = "all-MiniLM-L6-v2",
        hyp_dim: int = 16,
        device: str = "cpu",
        batch_size: int = 512,
    ):
        self.model = SentenceTransformer(model_name, device=device)
        euc_dim = self.model.get_embedding_dimension()
        self.projector = HyperbolicProjector(euc_dim, hyp_dim).to(device).eval()
        self.hyp_dim = hyp_dim
        self.batch_size = batch_size

    @property
    def dim(self) -> int:
        return self.hyp_dim

    def embed_batch(
        self,
        texts: list[str],
        show_progress: bool = True,
    ) -> np.ndarray:
        """Embed all texts -> Lorentz points (N, d+1)."""
        all_results = []
        for start in range(0, len(texts), self.batch_size):
            batch = texts[start : start + self.batch_size]
            euc = self.model.encode(
                batch, convert_to_tensor=True,
                show_progress_bar=False, normalize_embeddings=True,
                batch_size=self.batch_size,
            )
            with torch.no_grad():
                x_l = self.projector(euc.to(self.projector.projection.weight.device))
            all_results.append(x_l.cpu().numpy())
        return np.concatenate(all_results, axis=0)

    def embed_batch_euclidean(
        self,
        texts: list[str],
        show_progress: bool = True,
    ) -> np.ndarray:
        """Embed all texts -> Euclidean parametrization (N, d). Compact for storage."""
        all_results = []
        for start in range(0, len(texts), self.batch_size):
            batch = texts[start : start + self.batch_size]
            euc = self.model.encode(
                batch, convert_to_tensor=True,
                show_progress_bar=False, normalize_embeddings=True,
                batch_size=self.batch_size,
            )
            with torch.no_grad():
                z = self.projector.projection(
                    euc.to(self.projector.projection.weight.device)
                ) * self.projector.alpha
            all_results.append(z.cpu().numpy())
        return np.concatenate(all_results, axis=0)
