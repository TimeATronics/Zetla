#include "lorentz.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace hgnfs::lorentz {

//  inner product 

float inner(const LorentzPoint& a, const LorentzPoint& b) {
    float sum = -a.t * b.t;
    int d = static_cast<int>(a.x.size());
    for (int i = 0; i < d; ++i) sum += a.x[i] * b.x[i];
    return sum;
}

float norm(const LorentzPoint& v) {
    float dot = inner(v, v);
    return (dot < NEAR_ZERO_EPS) ? 0.0f : std::sqrt(dot);
}

//  distance 

float distance(const LorentzPoint& a, const LorentzPoint& b) {
    float in = inner(a, b);
    float arg = -in;
    if (arg < 1.0f + ARCCOSH_TOL) arg = 1.0f + ARCCOSH_TOL;
    return numerical::safe_arcosh(arg);
}

float squared_distance(const LorentzPoint& a, const LorentzPoint& b) {
    float d = distance(a, b);
    float sq = d * d;
    return (sq > SQUARED_DIST_CLAMP) ? SQUARED_DIST_CLAMP : sq;
}

//  exponential map 

LorentzPoint exp_o(const float* z, int dim) {
    float z_norm = 0.0f;
    for (int i = 0; i < dim; ++i) z_norm += z[i] * z[i];
    z_norm = std::sqrt(z_norm);
    if (z_norm < NEAR_ZERO_EPS) {
        LorentzPoint o(dim);
        o.t = 1.0f;
        return o;
    }
    float theta = numerical::clamp_norm(z_norm);
    float ch = std::cosh(theta);
    float sh = std::sinh(theta);
    LorentzPoint p(dim);
    p.t = ch;
    for (int i = 0; i < dim; ++i) p.x[i] = sh * z[i] / z_norm;
    return project(p);
}

LorentzPoint exp_x(const LorentzPoint& x, const LorentzPoint& v_tan) {
    int d = static_cast<int>(x.x.size());
    float vn = norm(v_tan);
    if (vn < NEAR_ZERO_EPS) return x;
    float ch = std::cosh(vn);
    float sh = std::sinh(vn);
    LorentzPoint r(d);
    r.t = ch * x.t + sh * v_tan.t / vn;
    for (int i = 0; i < d; ++i)
        r.x[i] = ch * x.x[i] + sh * v_tan.x[i] / vn;
    return project(r);
}

//  logarithmic map 

void log_o(const LorentzPoint& x, float* z_out, int dim) {
    float xs_norm = 0.0f;
    for (int i = 0; i < dim; ++i) xs_norm += x.x[i] * x.x[i];
    xs_norm = std::sqrt(xs_norm);
    if (xs_norm < NEAR_ZERO_EPS) {
        std::memset(z_out, 0, dim * sizeof(float));
        return;
    }
    float arg = x.t;
    if (arg < 1.0f + ARCCOSH_TOL) arg = 1.0f + ARCCOSH_TOL;
    float dist = numerical::safe_arcosh(arg);
    for (int i = 0; i < dim; ++i) z_out[i] = dist * x.x[i] / xs_norm;
}

LorentzPoint log_x(const LorentzPoint& x, const LorentzPoint& y) {
    int d = static_cast<int>(x.x.size());
    float in = inner(x, y) + 1.0f;
    if (in > -ARCCOSH_TOL) in = -ARCCOSH_TOL;
    in -= 1.0f;

    LorentzPoint u(d);
    u.t = y.t + in * x.t;
    for (int i = 0; i < d; ++i) u.x[i] = y.x[i] + in * x.x[i];

    float un = norm(u);
    if (un < NEAR_ZERO_EPS) {
        LorentzPoint zero(d);
        zero.t = 0.0f;
        return zero;
    }
    float dist = distance(x, y);
    LorentzPoint v(d);
    v.t = dist * u.t / un;
    for (int i = 0; i < d; ++i) v.x[i] = dist * u.x[i] / un;
    return v;
}

//  parallel transport 

LorentzPoint parallel_transport_o_to_x(const LorentzPoint& v, const LorentzPoint& x) {
    int d = static_cast<int>(x.x.size());
    float xs_norm = 0.0f;
    for (int i = 0; i < d; ++i) xs_norm += x.x[i] * x.x[i];
    xs_norm = std::sqrt(xs_norm);
    if (xs_norm < NEAR_ZERO_EPS) return v;

    float alpha = 0.0f;
    for (int i = 0; i < d; ++i) alpha += v.x[i] * x.x[i] / xs_norm;

    LorentzPoint r(d);
    r.t = v.t - alpha * (-xs_norm);
    for (int i = 0; i < d; ++i)
        r.x[i] = v.x[i] - alpha * ((1.0f - x.t) * x.x[i] / xs_norm);
    return r;
}

//  project onto manifold 

LorentzPoint project(const LorentzPoint& x) {
    int d = static_cast<int>(x.x.size());
    float xs_sq = 0.0f;
    for (int i = 0; i < d; ++i) xs_sq += x.x[i] * x.x[i];
    float x0 = std::sqrt(1.0f + xs_sq);
    if (x0 < NEAR_ZERO_EPS) x0 = 1.0f;
    LorentzPoint r(d);
    r.t = x0;
    r.x = x.x;
    return r;
}

bool is_on_manifold(const LorentzPoint& x, float eps) {
    if (x.t <= 0.0f) return false;
    float in = inner(x, x);
    float scale = x.t * x.t;
    if (scale < 1.0f) scale = 1.0f;
    return std::abs(in + 1.0f) < eps * scale;
}

//  batch operations 

void batch_exp_o(const float* Z, int N, int dim, float* X_out) {
    // X_out: N rows of (dim+1) floats [t, x1..xd]
    for (int n = 0; n < N; ++n) {
        const float* z = Z + n * dim;
        float* x_out = X_out + n * (dim + 1);

        float z_norm = 0.0f;
        for (int i = 0; i < dim; ++i) z_norm += z[i] * z[i];
        z_norm = std::sqrt(z_norm);

        if (z_norm < NEAR_ZERO_EPS) {
            x_out[0] = 1.0f;
            std::memset(x_out + 1, 0, dim * sizeof(float));
            continue;
        }
        float theta = numerical::clamp_norm(z_norm);
        float ch = std::cosh(theta);
        float sh = std::sinh(theta);
        x_out[0] = ch;
        for (int i = 0; i < dim; ++i)
            x_out[1 + i] = sh * z[i] / z_norm;
    }
}

void batch_inner_product(const float* Q, int M, const float* X, int N,
                         int dim, float* scores) {
    // Q: M × (dim+1), X: N × (dim+1), scores: M × N
    // ⟨q, x⟩_L = -q₀x₀ + Σqᵢxᵢ
    // Implementation: negate q's time, then dot
    for (int m = 0; m < M; ++m) {
        const float* q = Q + m * (dim + 1);
        float* score_row = scores + m * N;
        float q0_neg = -q[0];
        for (int n = 0; n < N; ++n) {
            const float* x = X + n * (dim + 1);
            float s = q0_neg * x[0];
            for (int i = 0; i < dim; ++i)
                s += q[1 + i] * x[1 + i];
            score_row[n] = s;
        }
    }
}

//  Einstein midpoint 

LorentzPoint einstein_midpoint(const LorentzPoint* points, int n,
                               const float* weights) {
    // Weighted Einstein midpoint in Klein coordinates
    int d = static_cast<int>(points[0].x.size());
    std::vector<float> num(d, 0.0f);
    float denom = 0.0f;

    for (int i = 0; i < n; ++i) {
        float w = (weights != nullptr) ? weights[i] : 1.0f;
        // Klein coordinates: k = x_space / x_time
        float k_norm2 = 0.0f;
        std::vector<float> k(d);
        float x0_clamp = std::max(points[i].t, NEAR_ZERO_EPS);
        for (int j = 0; j < d; ++j) {
            k[j] = points[i].x[j] / x0_clamp;
            k_norm2 += k[j] * k[j];
        }
        float gamma = 1.0f / std::sqrt(std::max(1.0f - k_norm2, NEAR_ZERO_EPS));
        float phi = w * gamma;
        for (int j = 0; j < d; ++j) num[j] += phi * gamma * k[j];
        denom += phi * gamma;
    }

    if (denom < NEAR_ZERO_EPS) {
        LorentzPoint o(d);
        o.t = 1.0f;
        return o;
    }

    float m_k_norm2 = 0.0f;
    std::vector<float> m_k(d, 0.0f);
    for (int j = 0; j < d; ++j) {
        m_k[j] = num[j] / denom;
        m_k_norm2 += m_k[j] * m_k[j];
    }

    float dn = std::sqrt(std::max(1.0f - m_k_norm2, NEAR_ZERO_EPS));
    LorentzPoint result(d);
    result.t = 1.0f / dn;
    for (int j = 0; j < d; ++j) result.x[j] = m_k[j] / dn;
    return project(result);
}

}  // namespace hgnfs::lorentz
