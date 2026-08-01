#include "pca.hpp"
#include <Eigen/Dense>
#include <Eigen/SVD>
#include <cstring>

namespace hgnfs::projector {

void PCAProjector::fit(const float* data, int n_samples, int input_dim, int target_dim) {
    input_dim_ = input_dim;
    target_dim_ = target_dim;
    if (n_samples == 0 || input_dim == 0 || target_dim == 0) return;

    // Copy to Eigen
    Eigen::MatrixXf M(n_samples, input_dim);
    for (int i = 0; i < n_samples; ++i)
        for (int j = 0; j < input_dim; ++j)
            M(i, j) = data[i * input_dim + j];

    // Center
    mean_.resize(input_dim);
    for (int j = 0; j < input_dim; ++j)
        mean_[j] = M.col(j).mean();
    for (int i = 0; i < n_samples; ++i)
        for (int j = 0; j < input_dim; ++j)
            M(i, j) -= mean_[j];

    // SVD: M = U Σ V^T - columns of V are principal directions
    Eigen::BDCSVD<Eigen::MatrixXf> svd;
    svd.compute(M, Eigen::ComputeThinV);
    const auto& V = svd.matrixV();  // input_dim × rank

    // Take top target_dim components
    int k = std::min(target_dim, static_cast<int>(V.cols()));
    components_.resize(k * input_dim);
    for (int c = 0; c < k; ++c)
        for (int j = 0; j < input_dim; ++j)
            components_[c * input_dim + j] = V(j, c);
}

void PCAProjector::transform(const float* x, int input_dim,
                             float* out, int target_dim) const {
    int k = std::min(target_dim, target_dim_);
    for (int c = 0; c < k; ++c) {
        float val = 0.0f;
        for (int j = 0; j < input_dim; ++j)
            val += components_[c * input_dim + j] * (x[j] - mean_[j]);
        out[c] = val;
    }
    for (int c = k; c < target_dim; ++c) out[c] = 0.0f;
}

void PCAProjector::transform_batch(const float* X, int n, int input_dim,
                                   float* out, int target_dim) const {
    for (int i = 0; i < n; ++i)
        transform(X + i * input_dim, input_dim, out + i * target_dim, target_dim);
}

}  // namespace hgnfs::projector
