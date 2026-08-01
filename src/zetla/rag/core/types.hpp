#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace hgnfs {

//  configuration constants 

inline constexpr int    DEFAULT_DIM         = 128;   // recommended 128D
inline constexpr float  COSH_SINH_CLIP      = 15.0f;
inline constexpr float  NEAR_ZERO_EPS       = 1e-12f;
inline constexpr float  ARCCOSH_TOL         = 1e-6f;
inline constexpr float  SQUARED_DIST_CLAMP  = 50.0f;
inline constexpr float  NORM_CLAMP          = 3.0f;   // clamp embedding norms

//  Lorentz point 

struct LorentzPoint {
    float t;                     // time component x₀
    std::vector<float> x;        // spatial components x₁...x_d

    LorentzPoint() : t(1.0f), x(DEFAULT_DIM, 0.0f) {}
    explicit LorentzPoint(int dim) : t(1.0f), x(dim, 0.0f) {}
    LorentzPoint(float t_, std::vector<float> x_) : t(t_), x(std::move(x_)) {}

    int dim() const { return static_cast<int>(x.size()); }
};

//  chunk metadata 

struct ChunkMeta {
    std::string path;
    int chunk_idx = 0;
    std::string label = "text";    // optional: "text", "code", "image"
};

//  search result 

struct SearchResult {
    int chunk_idx = 0;
    std::string path;
    float score = 0.0f;          // Lorentz inner product (higher = closer)
    float distance = 0.0f;       // geodesic distance
    std::string text_snippet;    // first 200 chars of chunk
};

//  raf: raw float array (for storing euclidean params) 

using FloatVec = std::vector<float>;

}  // namespace hgnfs
