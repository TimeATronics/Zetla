#pragma once
#include "../core/types.hpp"
#include <vector>

namespace hgnfs::training {

struct ContrastiveConfig {
    int epochs = 5;
    int batch_size = 512;
    float lr = 0.01f;
    float temperature = 0.07f;
    float weight_decay = 0.0f;
};

//  Projection trainer 

/// Trains a linear projection W(128×384) from BGE embeddings to hyperbolic
/// Euclidean params using contrastive InfoNCE loss.
///
/// Input:  E: N × 384  (BGE Euclidean embeddings, frozen)
///         labels: N   (document IDs - chunks from same doc = positive)
/// Output: W:  128×384  (projection matrix, initialized as-is, updated in-place)
///         loss history per epoch
std::vector<float> train_projection(
    const float* E,           // N × 384
    const int* labels,        // N
    int N, int euc_dim, int hyp_dim,
    float* W,                 // hyp_dim × euc_dim  [in/out]
    const ContrastiveConfig& cfg = {}
);

}  // namespace hgnfs::training
