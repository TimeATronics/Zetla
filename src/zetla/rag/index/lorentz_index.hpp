#pragma once
#include "../core/types.hpp"
#include <string>
#include <vector>

namespace hgnfs::index {

class LorentzIndex {
public:
    explicit LorentzIndex(int dim = DEFAULT_DIM);

    void add(const float* z, int dim, ChunkMeta meta);
    void add_batch(const float* Z, int n, int dim,
                   const std::vector<ChunkMeta>& metas);

    std::vector<SearchResult> search(
        const float* query_z, int dim,
        int top_k = 10,
        const char* scope_path = nullptr
    );

    // Hyperbolic reranking: compute Lorentz inner products for specific candidates
    // Returns scores (higher = more similar). Blazing fast - O(candidates.size() * dim)
    std::vector<float> rerank(const float* query_z, int dim,
                               const std::vector<int>& candidates) const;

    int n_chunks() const { return static_cast<int>(meta_.size()); }
    int dim() const { return dim_; }
    size_t memory_bytes() const;

    const std::vector<float>& raw_data() const { return Z_; }
    std::vector<ChunkMeta> meta_snapshot() const { return meta_; }

    bool save(const std::string& path) const;
    bool load(const std::string& path);

private:
    int dim_;
    std::vector<float> Z_;
    std::vector<ChunkMeta> meta_;
};

}  // namespace hgnfs::index
