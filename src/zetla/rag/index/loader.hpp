#pragma once
#include "../core/types.hpp"
#include <string>
#include <vector>

namespace hgnfs::loader {

struct IndexData {
    int dim;
    std::vector<float> Z;              // n_chunks × dim, Euclidean params
    std::vector<ChunkMeta> metas;      // per-chunk metadata
    std::vector<std::string> paths;    // unique paths
    // PCA projection
    int pca_input_dim;
    int pca_target_dim;
    std::vector<float> pca_mean;       // input_dim
    std::vector<float> pca_components; // target_dim × input_dim (row-major)
};

IndexData load_binary(const char* filepath);

}  // namespace hgnfs::loader
