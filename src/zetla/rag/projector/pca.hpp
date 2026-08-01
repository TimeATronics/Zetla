#pragma once
#include <vector>

namespace hgnfs::projector {

class PCAProjector {
public:
    PCAProjector() = default;

    // Fit PCA to reduce from input_dim -> target_dim
    void fit(const float* data, int n_samples, int input_dim, int target_dim);

    // Transform one vector (input_dim -> target_dim)
    void transform(const float* x, int input_dim, float* out, int target_dim) const;

    // Transform batch
    void transform_batch(const float* X, int n, int input_dim,
                         float* out, int target_dim) const;

    int target_dim() const { return target_dim_; }
    int input_dim() const { return input_dim_; }

private:
    int input_dim_ = 0;
    int target_dim_ = 0;
    std::vector<float> components_;   // target_dim × input_dim (row-major)
    std::vector<float> mean_;         // input_dim
};

}  // namespace hgnfs::projector
