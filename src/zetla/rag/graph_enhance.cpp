#include "graph_enhance.hpp"
#include "core/lorentz.hpp"
#include "core/types.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace hgnfs::gnn {

std::vector<std::vector<int>> build_knn_graph(
    const float* X, int N, int dim, int k) {
    std::vector<std::vector<int>> adj(N);
    if (N <= 1) return adj;

    k = std::min(k, N - 1);

    for (int i = 0; i < N; ++i) {
        const float* xi = &X[i * dim];
        std::vector<std::pair<float, int>> scores;
        scores.reserve(N - 1);

        for (int j = 0; j < N; ++j) {
            if (i == j) continue;
            const float* xj = &X[j * dim];
            // Lorentz inner product: ⟨xi, xj⟩ = -xi₀xj₀ + Σ xik xjk
            float inn = -xi[0] * xj[0];
            for (int d = 1; d < dim; ++d) {
                inn += xi[d] * xj[d];
            }
            scores.push_back({inn, j});
        }

        // Sort descending (higher inner product = closer)
        std::partial_sort(scores.begin(), scores.begin() + k, scores.end(),
            [](auto& a, auto& b) { return a.first > b.first; });

        adj[i].reserve(k);
        for (int t = 0; t < k; ++t) {
            adj[i].push_back(scores[t].second);
        }
    }

    return adj;
}

void smooth_embeddings(
    const std::vector<float>& X_in, int N, int dim,
    const std::vector<std::vector<int>>& adj,
    std::vector<float>& X_out) {
    X_out = X_in;  // copy

    for (int i = 0; i < N; ++i) {
        // Collect points: self + neighbors
        std::vector<LorentzPoint> points;
        {
            const float* p = &X_in[i * dim];
            LorentzPoint lp;
            lp.t = p[0];
            lp.x.assign(p + 1, p + dim);
            points.push_back(lp);
        }
        for (int nb : adj[i]) {
            const float* p = &X_in[nb * dim];
            LorentzPoint lp;
            lp.t = p[0];
            lp.x.assign(p + 1, p + dim);
            points.push_back(lp);
        }

        // Einstein midpoint of {self + neighbors}
        auto mid = lorentz::einstein_midpoint(points.data(), (int)points.size(), nullptr);

        // Write back
        float* out = &X_out[i * dim];
        out[0] = mid.t;
        for (int d = 0; d < dim - 1; ++d) {
            out[d + 1] = mid.x[d];
        }
    }
}

}  // namespace hgnfs::gnn
