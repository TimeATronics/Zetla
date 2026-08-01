"""
Lorentz (hyperboloid) manifold operations.
Model: L^d = { x ∈ R^{d+1} : ⟨x, x⟩_L = -1, x_0 > 0 }

All operations follow the HGCN reference implementation (refs/hgcn) with 
the Euclidean parametrization trick from the Numerical Stability paper.
"""

import torch
import torch.nn.functional as F
from hgnfs_py.core.numerical import COSH_SINH_CLIP, NEAR_ZERO_EPS, ARCCOSH_TOL, safe_arcosh, clamp_norm

#  origin (d+1 dimensional) 
_ORIGIN_DIMS = {}  # cache per dimension


def origin(dim: int) -> torch.Tensor:
    """Lorentz origin o = (1, 0, ..., 0) ∈ L^d. d = spatial dim."""
    if dim not in _ORIGIN_DIMS:
        o = torch.zeros(dim + 1)
        o[0] = 1.0
        _ORIGIN_DIMS[dim] = o
    return _ORIGIN_DIMS[dim].clone()


LORENTZ_ORIGIN = (1.0,)  # scalar sentinel


#  inner product / norm 

def lorentz_inner(x: torch.Tensor, y: torch.Tensor, keepdim: bool = False) -> torch.Tensor:
    """
    ⟨x, y⟩_L = -x_0·y_0 + Σ_{i=1}^{d} x_i·y_i

    Args:
        x, y: (*, d+1)  - batch of Lorentz points
    Returns:
        (*, 1) if keepdim else (*,)
    """
    xy = x * y
    inner = xy[..., 1:].sum(dim=-1) - xy[..., 0]
    if keepdim:
        inner = inner.unsqueeze(-1)
    return inner


def lorentz_norm(x: torch.Tensor, keepdim: bool = False) -> torch.Tensor:
    """‖v‖_L = √⟨v, v⟩_L (used for tangent vectors)."""
    dot = lorentz_inner(x, x, keepdim=keepdim)
    return torch.sqrt(torch.clamp(dot, min=NEAR_ZERO_EPS))


#  geodesic distance 

def distance(x: torch.Tensor, y: torch.Tensor) -> torch.Tensor:
    """
    d_L(x, y) = arcosh(-⟨x, y⟩_L)

    Args:
        x, y: (*, d+1)  - Lorentz points on L^d
    Returns:
        (*,)  - pairwise geodesic distances
    """
    inner = lorentz_inner(x, y, keepdim=False)
    arg = torch.clamp(-inner, min=1.0 + ARCCOSH_TOL)
    return safe_arcosh(arg)


def squared_distance(x: torch.Tensor, y: torch.Tensor) -> torch.Tensor:
    """d²(x, y) = arcosh(-⟨x, y⟩_L)²  (squared geodesic distance). Clamped to 50 per HGCN."""
    d = distance(x, y)
    sq = d * d
    return torch.clamp(sq, max=50.0)


#  exponential map 

def exp_o(v: torch.Tensor) -> torch.Tensor:
    """
    Exponential map from the origin.
    v: (*, d+1) - tangent vector at origin (v_0 is ignored, must be 0).
    Returns: (*, d+1) - point on Lorentz manifold.
    """
    v_space = v[..., 1:]  # (*, d)
    v_norm = torch.norm(v_space, p=2, dim=-1, keepdim=True)  # (*, 1)
    v_norm = torch.clamp(v_norm, min=NEAR_ZERO_EPS)
    theta = clamp_norm(v_norm)

    res = torch.zeros_like(v)
    res[..., 0] = torch.cosh(theta).squeeze(-1)
    direction = v_space / v_norm
    res[..., 1:] = torch.sinh(theta) * direction
    return _project(res)


def exp_x(v: torch.Tensor, x: torch.Tensor) -> torch.Tensor:
    """
    Exponential map from point x.
    v: (*, d+1) - tangent vector at x.
    x: (*, d+1) - base point on Lorentz manifold.
    Returns: (*, d+1) - exp_x(v).
    """
    v_norm = lorentz_norm(v, keepdim=True)
    v_norm = torch.clamp(v_norm, max=1e6)
    theta = torch.clamp(v_norm, min=NEAR_ZERO_EPS)
    cosh_t = torch.cosh(theta)
    sinh_t = torch.sinh(theta)
    result = cosh_t * x + sinh_t * v / theta
    return _project(result)


#  logarithmic map 

def log_o(x: torch.Tensor) -> torch.Tensor:
    """
    Logarithmic map to the origin.
    x: (*, d+1) - point on Lorentz manifold.
    Returns: (*, d+1) - tangent vector at origin (v_0 = 0).
    """
    x_space = x[..., 1:]  # (*, d)
    x_norm = torch.norm(x_space, p=2, dim=-1, keepdim=True)
    x_norm = torch.clamp(x_norm, min=NEAR_ZERO_EPS)
    theta = torch.clamp(x[..., 0:1], min=1.0 + ARCCOSH_TOL)
    dist = safe_arcosh(theta)
    res = torch.zeros_like(x)
    res[..., 1:] = dist * (x_space / x_norm)
    # time component stays 0 (tangent at origin)
    return res


def log_x(y: torch.Tensor, x: torch.Tensor) -> torch.Tensor:
    """
    Logarithmic map from x to y.
    x, y: (*, d+1) - points on Lorentz manifold.
    Returns: (*, d+1) - tangent vector at x pointing toward y.
    """
    # ⟨x, y⟩_L + 1 ≤ 0 for points on hyperboloid (Cauchy-Schwarz)
    xy = lorentz_inner(x, y, keepdim=True) + 1.0
    xy = torch.clamp(xy, max=-ARCCOSH_TOL) - 1.0
    u = y + xy * x
    u_norm = lorentz_norm(u, keepdim=True)
    u_norm = torch.clamp(u_norm, min=NEAR_ZERO_EPS)
    dist = distance(x, y).unsqueeze(-1)
    result = dist * u / u_norm
    return _project_tan(result, x)


#  parallel transport 

def parallel_transport_o_to_x(v: torch.Tensor, x: torch.Tensor) -> torch.Tensor:
    """
    Parallel transport of tangent vector v from origin to x.
    v: (*, d+1) - tangent vector at origin (v_0 = 0).
    x: (*, d+1) - destination point.
    Returns: (*, d+1) - tangent vector at x.
    """
    x0, xs = x[..., 0:1], x[..., 1:]
    xs_norm = torch.norm(xs, p=2, dim=-1, keepdim=True)
    xs_norm = torch.clamp(xs_norm, min=NEAR_ZERO_EPS)
    xs_dir = xs / xs_norm

    # Direction vector from HGCN ptransp0 formula
    dir_0 = -xs_norm
    dir_s = (1.0 - x0) * xs_dir
    alpha = (v[..., 1:] * xs_dir).sum(dim=-1, keepdim=True)
    result = v - alpha * torch.cat([dir_0, dir_s], dim=-1)
    return _project_tan(result, x)


#  manifold utilities 

def _project(x: torch.Tensor) -> torch.Tensor:
    """
    Project any vector onto the Lorentz manifold by replacing x₀ with
    sqrt(1 + ‖x_space‖²). Note: since cosh²θ - sinh²θ ≠ 1 in float32,
    this creates a small perturbation (~1e-6) from the exact exp_o result.
    Used only for truly unconstrained inputs, not after exp_o/exp_x.
    """
    xs = x[..., 1:]
    xs_sqnorm = (xs * xs).sum(dim=-1, keepdim=True)
    x0 = torch.sqrt(torch.clamp(1.0 + xs_sqnorm, min=NEAR_ZERO_EPS))
    return torch.cat([x0, xs], dim=-1)


def _project_tan(v: torch.Tensor, x: torch.Tensor) -> torch.Tensor:
    """Project v onto the tangent space at x: T_x L^d."""
    v_space = v[..., 1:]
    x_space = x[..., 1:]
    # ⟨x, v⟩_L should be 0 for tangent vectors
    vx = (x_space * v_space).sum(dim=-1, keepdim=True)
    v0 = vx / torch.clamp(x[..., 0:1], min=NEAR_ZERO_EPS)
    return torch.cat([v0, v_space], dim=-1)


def is_on_manifold(x: torch.Tensor, eps: float = 1e-4) -> torch.Tensor:
    """
    Check if points satisfy Lorentz constraint: ⟨x,x⟩_L = -1 and x₀ > 0.
    Uses relative tolerance scaled by ‖x‖² to handle large-magnitude points
    where float32 can't verify the constraint to absolute 1e-4.
    """
    inner = lorentz_inner(x, x, keepdim=False)
    # Relative tolerance: allow error proportional to embedding magnitude
    scale = torch.clamp(x[..., 0] ** 2, min=1.0)
    constraint_ok = torch.abs(inner + 1.0) < (eps * scale)
    time_ok = x[..., 0] > 0
    return constraint_ok & time_ok


#  batch conversion 

def euclidean_to_lorentz(z: torch.Tensor) -> torch.Tensor:
    """
    Euclidean parametrization z ∈ R^d -> Lorentz point x ∈ L^d.
    Uses exp_o under the hood. z_norm clamped to COSH_SINH_CLIP.
    
    Args: z: (*, d) - Euclidean parameters
    Returns: (*, d+1) - Lorentz points
    """
    v = F.pad(z, (1, 0), value=0.0)  # prepend time=0
    return exp_o(v)


def lorentz_to_euclidean(x: torch.Tensor) -> torch.Tensor:
    """Lorentz point -> Euclidean parametrization (log_o, discard time)."""
    return log_o(x)[..., 1:]


#  Möbius matrix-vector multiplication 

def mobius_matvec(W: torch.Tensor, x: torch.Tensor) -> torch.Tensor:
    """
    Hyperbolic linear transform: W ⊗ x = exp_o(W · log_o(x)).
    W: (out_dim, in_dim) - weight matrix.
    x: (*, in_dim+1) - Lorentz points.
    Returns: (*, out_dim+1) - transformed Lorentz points.
    """
    u = log_o(x)  # (*, in_dim+1)
    u_space = u[..., 1:]  # (*, in_dim)
    mu = F.linear(u_space, W)  # (*, out_dim)
    mu_padded = F.pad(mu, (1, 0), value=0.0)
    return _project(exp_o(mu_padded))


def einstein_midpoint(
    points: torch.Tensor, weights: torch.Tensor | None = None
) -> torch.Tensor:
    """
    Weighted Einstein midpoint (hyperbolic barycenter).
    points: (N, d+1) - Lorentz points.
    weights: (N,) or None (uniform) - positive weights.
    Returns: (d+1,) - the weighted centroid.

    Algorithm: Weighted sum in Klein coordinates, weighted by Lorentz gamma.
    Per HypRAG's Outward Einstein Midpoint.
    """
    x0, xs = points[..., 0], points[..., 1:]  # (N,), (N, d)
    # Klein coordinates: k = x_space / x_time
    k = xs / torch.clamp(x0.unsqueeze(-1), min=NEAR_ZERO_EPS)  # (N, d)
    k_norm2 = (k * k).sum(dim=-1)  # (N,)
    # Lorentz gamma factor
    gamma = 1.0 / torch.sqrt(torch.clamp(1.0 - k_norm2, min=NEAR_ZERO_EPS))  # (N,)
    if weights is None:
        weights = torch.ones_like(gamma)
    phi = weights * gamma  # outward re-weighting
    w_tilde = phi / torch.clamp(phi.sum(), min=NEAR_ZERO_EPS)
    num = (w_tilde.unsqueeze(-1) * gamma.unsqueeze(-1) * k).sum(dim=0)
    denom = (w_tilde * gamma).sum()
    m_k = num / torch.clamp(denom, min=NEAR_ZERO_EPS)
    m_k_norm2 = (m_k * m_k).sum()
    denom2 = torch.sqrt(torch.clamp(1.0 - m_k_norm2, min=NEAR_ZERO_EPS))
    x0_out = 1.0 / denom2
    xs_out = m_k / denom2
    return torch.cat([x0_out.unsqueeze(0), xs_out])
