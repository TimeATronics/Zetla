#pragma once
#include <vector>
#include <cstdint>

namespace hgnfs::gnn {

// Build k-NN graph from hyperbolic embeddings using Lorentz inner product
// X: flat array of N × dim floats (hyperbolic vectors: [time, x1...xd])
// Returns adjacency list: adj[i] = list of neighbor indices
std::vector<std::vector<int>> build_knn_graph(
    const float* X, int N, int dim, int k = 5);

// Graph smoothing: averages each point's embedding with its neighbors in hyperbolic space
// Einstein midpoint of {x_i} ∪ {neighbors of x_i}
void smooth_embeddings(
    const std::vector<float>& X_in, int N, int dim,
    const std::vector<std::vector<int>>& adj,
    std::vector<float>& X_out);

}  // namespace hgnfs::gnn
