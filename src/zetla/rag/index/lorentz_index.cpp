#include "lorentz_index.hpp"
#include "../core/lorentz.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>

namespace hgnfs::index {

LorentzIndex::LorentzIndex(int dim) : dim_(dim) {}

void LorentzIndex::add(const float* z, int dim, ChunkMeta meta) {
    Z_.insert(Z_.end(), z, z + dim);
    meta_.push_back(std::move(meta));
}

void LorentzIndex::add_batch(const float* Z, int n, int dim,
                             const std::vector<ChunkMeta>& metas) {
    Z_.insert(Z_.end(), Z, Z + n * dim);
    meta_.insert(meta_.end(), metas.begin(), metas.end());
}

std::vector<float> LorentzIndex::rerank(const float* query_z, int dim,
                                         const std::vector<int>& candidates) const {
    std::vector<float> scores(candidates.size());
    for (size_t ci = 0; ci < candidates.size(); ++ci) {
        int idx = candidates[ci];
        if (idx < 0 || idx >= n_chunks()) { scores[ci] = -1e20f; continue; }
        const float* chunk = &Z_[idx * dim];
        float inn = -query_z[0] * chunk[0];
        for (int j = 1; j < dim; ++j) inn += query_z[j] * chunk[j];
        scores[ci] = inn;
    }
    return scores;
}

std::vector<SearchResult> LorentzIndex::search(
    const float* query_z, int dim, int top_k, const char* scope_path) {
    int N = n_chunks();
    if (N == 0) return {};

    // Z_ stores (dim+1)-dimensional hyperbolic vectors
    // query_z is also (dim+1)-dimensional hyperbolic vector
    // dim = hyperboloid dimension (e.g., 65 for 64-dim word embeddings)

    // Compute Lorentz inner products directly: ⟨q, chunk⟩ = -q₀c₀ + Σqᵢcᵢ
    std::vector<float> scores(N);
    for (int i = 0; i < N; ++i) {
        const float* chunk = &Z_[i * dim];
        float inn = -query_z[0] * chunk[0];  // time component with negative sign
        for (int j = 1; j < dim; ++j) {
            inn += query_z[j] * chunk[j];     // spatial components
        }
        scores[i] = inn;
    }

    // Scope filter
    if (scope_path) {
        for (int i = 0; i < N; ++i) {
            if (meta_[i].path != scope_path) scores[i] = -1e20f;
        }
    }

    // Top-K by score (higher Lorentz inner = more similar)
    std::vector<int> indices(N);
    for (int i = 0; i < N; ++i) indices[i] = i;

    int k = std::min(top_k, N);
    std::partial_sort(indices.begin(), indices.begin() + k, indices.end(),
        [&](int a, int b) { return scores[a] > scores[b]; });

    std::vector<SearchResult> results;
    results.reserve(k);
    for (int i = 0; i < k; ++i) {
        int idx = indices[i];
        if (scores[idx] <= -1e10f) continue;
        SearchResult r;
        r.chunk_idx = meta_[idx].chunk_idx;
        r.path = meta_[idx].path;
        r.score = scores[idx];
        // Distance from Lorentz inner product: d = arccosh(-⟨x,y⟩)
        // For points on hyperboloid, ⟨x,x⟩ = -1, so -⟨x,y⟩ >= 1
        float arg = std::max(-scores[idx], 1.0f + 1e-6f);
        r.distance = numerical::safe_arcosh(arg);
        results.push_back(r);
    }
    return results;
}

size_t LorentzIndex::memory_bytes() const {
    return Z_.size() * sizeof(float) + meta_.size() * sizeof(ChunkMeta);
}

bool LorentzIndex::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    int32_t header[3] = {dim_, n_chunks(), (int)meta_.size()};
    f.write((const char*)header, sizeof(header));
    f.write((const char*)Z_.data(), Z_.size() * sizeof(float));
    for (auto& m : meta_) {
        int32_t plen = (int32_t)m.path.size();
        f.write((const char*)&m.chunk_idx, sizeof(m.chunk_idx));
        f.write((const char*)&plen, sizeof(plen));
        f.write(m.path.data(), plen);
    }
    return f.good();
}

bool LorentzIndex::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    int32_t header[3];
    f.read((char*)header, sizeof(header));
    dim_ = header[0]; int n = header[1], mcnt = header[2];
    Z_.resize(n * dim_);
    f.read((char*)Z_.data(), Z_.size() * sizeof(float));
    meta_.clear(); meta_.reserve(mcnt);
    for (int i = 0; i < mcnt; ++i) {
        ChunkMeta m;
        f.read((char*)&m.chunk_idx, sizeof(m.chunk_idx));
        int32_t plen; f.read((char*)&plen, sizeof(plen));
        m.path.resize(plen);
        f.read(&m.path[0], plen);
        meta_.push_back(m);
    }
    return f.good() || f.eof();
}

}  // namespace hgnfs::index
