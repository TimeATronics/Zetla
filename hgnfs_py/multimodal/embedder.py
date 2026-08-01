"""
Euclidean -> Hyperbolic embedding projection.

Uses BGE-Micro for text embedding, then projects into Lorentz space
via a learnable linear projection + exp_o. Follows the MERU/HypRAG approach.

Install: uv pip install sentence-transformers
"""
from __future__ import annotations

import numpy as np
import torch
import torch.nn as nn


class HyperbolicProjector(nn.Module):
    """
    Projects Euclidean embeddings into the Lorentz hyperboloid.

    Pipeline:  text -> BGE-Micro (384D Euclidean) -> Linear(384 -> d) -> exp₀ -> L^d

    The linear projection has a learnable per-modality scale α (from MERU).
    """

    def __init__(self, euc_dim: int = 384, hyp_dim: int = 16, alpha: float = 1.0):
        super().__init__()
        self.euc_dim = euc_dim
        self.hyp_dim = hyp_dim
        self.projection = nn.Linear(euc_dim, hyp_dim, bias=False)
        self.alpha = nn.Parameter(torch.tensor(alpha))

    def forward(self, x_euc: torch.Tensor) -> torch.Tensor:
        """
        Args:
            x_euc: (*, euc_dim) - Euclidean embeddings (from BGE).
        Returns:
            (*, hyp_dim+1) - Lorentz points on L^{hyp_dim}.
        """
        from hgnfs_py.core.lorentz import exp_o

        # Scale and project
        z = self.projection(x_euc) * self.alpha  # (*, hyp_dim)
        # Clamp norm for numerical safety
        z_norm = torch.norm(z, dim=-1, keepdim=True)
        z = z * torch.clamp(15.0 / z_norm.clamp(min=1e-8), max=1.0)
        # exp_o: pad time=0, apply exponential map
        v = torch.nn.functional.pad(z, (1, 0), value=0.0)
        return exp_o(v)


class TextEmbedder:
    """
    Convenience wrapper: text -> hyperbolic Lorentz point.

    Usage:
        embedder = TextEmbedder(model_name="all-MiniLM-L6-v2", hyp_dim=16)
        x_lorentz = embedder.embed("hello world")  # (17,) Lorentz point
    """

    def __init__(
        self,
        model_name: str = "all-MiniLM-L6-v2",
        hyp_dim: int = 16,
        device: str = "cpu",
    ):
        from sentence_transformers import SentenceTransformer

        self.model = SentenceTransformer(model_name, device=device)
        euc_dim = self.model.get_embedding_dimension()
        self.projector = HyperbolicProjector(euc_dim, hyp_dim)
        self.hyp_dim = hyp_dim
        self.device = device

        # Move projector to same device
        self.projector = self.projector.to(device)
        self.projector.eval()

    @property
    def dim(self) -> int:
        return self.hyp_dim

    def embed(self, texts: str | list[str]) -> np.ndarray:
        """
        Embed text(s) to Lorentz points.

        Args:
            texts: single string or list of strings.
        Returns:
            (d+1,) or (N, d+1) - Lorentz points as numpy arrays.
        """
        single = isinstance(texts, str)
        if single:
            texts = [texts]

        # Euclidean embedding
        euc = self.model.encode(
            texts,
            convert_to_tensor=True,
            show_progress_bar=False,
            normalize_embeddings=True,
        )
        if euc.device != self.projector.projection.weight.device:
            euc = euc.to(self.projector.projection.weight.device)
        euc = euc.clone()

        with torch.no_grad():
            x_lorentz = self.projector(euc)
        result = x_lorentz.cpu().numpy()

        return result[0] if single else result

    def embed_to_euclidean_param(self, texts: str | list[str]) -> np.ndarray:
        """
        Embed text(s) and return Euclidean parametrization z ∈ R^d (not Lorentz).
        This is more compact for storage and training.
        """
        single = isinstance(texts, str)
        if single:
            texts = [texts]

        euc = self.model.encode(
            texts,
            convert_to_tensor=True,
            show_progress_bar=False,
            normalize_embeddings=True,
        )
        if euc.device != self.projector.projection.weight.device:
            euc = euc.to(self.projector.projection.weight.device)
        euc = euc.clone()

        with torch.no_grad():
            z = self.projector.projection(euc) * self.projector.alpha
        result = z.cpu().numpy()
        return result[0] if single else result


#  lightweight fallback (no model download needed) 

class MockEmbedder:
    """Fallback: hash-based stable random embeddings. Same interface as TextEmbedder."""

    def __init__(self, hyp_dim: int = 16):
        self.hyp_dim = hyp_dim

    @property
    def dim(self) -> int:
        return self.hyp_dim

    def embed(self, texts: str | list[str]) -> np.ndarray:
        single = isinstance(texts, str)
        if single:
            texts = [texts]
        result = np.array([_mock_vec(t, self.dim) for t in texts], dtype=np.float32)
        from hgnfs_py.core.lorentz import exp_o
        v = torch.nn.functional.pad(torch.from_numpy(result), (1, 0), value=0.0)
        x = exp_o(v)
        return x.numpy()[0] if single else x.numpy()

    def embed_to_euclidean_param(self, texts: str | list[str]) -> np.ndarray:
        single = isinstance(texts, str)
        if single:
            texts = [texts]
        result = np.array([_mock_vec(t, self.dim) for t in texts], dtype=np.float32)
        return result[0] if single else result


def _mock_vec(text: str, dim: int) -> np.ndarray:
    h = hash(text) & 0xFFFFFFFF
    rng = np.random.RandomState(h)
    return rng.randn(dim).astype(np.float32) * 0.1


#  factory 

def create_embedder(
    use_real: bool = True,
    model_name: str = "all-MiniLM-L6-v2",
    hyp_dim: int = 16,
    device: str = "cpu",
):
    """Create an embedder. If use_real=False, returns MockEmbedder."""
    if use_real:
        try:
            return TextEmbedder(model_name=model_name, hyp_dim=hyp_dim, device=device)
        except Exception as e:
            print(f"Could not load model '{model_name}': {e}. Falling back to mock.")
    return MockEmbedder(hyp_dim=hyp_dim)
