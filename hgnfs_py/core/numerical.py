"""
Numerical stability utilities for hyperbolic operations.
Clamps and epsilon values derived from the Numerical Stability paper
(Mishne et al., ICML 2023) and HGCN reference implementation.
"""

import math
import torch

#  numerical constants 

COSH_SINH_CLIP = 15.0       # cosh(15) ≈ 1.6M; cosh(19) overflows fp32
NEAR_ZERO_EPS = 1e-12       # minimum norm before treating as zero
ARCCOSH_TOL = 1e-6          # arcosh safety margin (arg ≥ 1 + ε)
TANH_CLIP = 15.0            # tanh(15) ≈ 0.999999; tanh(19) = 1.0 exactly
EXP_NORM_MAX = 15.0         # max ‖z‖ before cosh overflow


def clamp_norm(z: torch.Tensor) -> torch.Tensor:
    """Clamp Euclidean parametrization norm to safe range for cosh/sinh."""
    return torch.clamp(z, max=EXP_NORM_MAX)


def safe_arcosh(x: torch.Tensor) -> torch.Tensor:
    """Numerically stable arcosh. Clamps input ≥ 1+eps, computes in float64."""
    x = torch.clamp(x, min=1.0 + ARCCOSH_TOL)
    # Stable formula: arcosh(x) = ln(x + sqrt(x² - 1))
    z = x.double()
    return (z + torch.sqrt(z * z - 1.0)).clamp_min(1e-15).log().to(x.dtype)


def safe_tanh(x: torch.Tensor) -> torch.Tensor:
    """Numerically stable tanh with arg clamping."""
    return torch.clamp(x, -TANH_CLIP, TANH_CLIP).tanh()


def safe_artanh(x: torch.Tensor) -> torch.Tensor:
    """Numerically stable artanh. Clamps arg to [-1+ε, 1-ε]."""
    x = torch.clamp(x, -1.0 + ARCCOSH_TOL, 1.0 - ARCCOSH_TOL)
    z = x.double()
    return 0.5 * (torch.log1p(z) - torch.log1p(-z)).to(x.dtype)


def estimate_max_radius(dim: int = 16, target_volume: float = 1e6) -> float:
    """
    Estimate the manifold radius needed to accommodate `target_volume` points
    in `dim`-dimensional Lorentz space. Useful for initialization scaling.
    """
    # Volume ~ sinh(r)^{dim-1}, want sinh(r) ~ target_volume^{1/(dim-1)}
    return float(math.asinh(target_volume ** (1.0 / max(dim - 1, 1))))
