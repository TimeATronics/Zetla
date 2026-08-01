#include "hgcn.hpp"
#include <cmath>
#include <cstring>
#include <random>

namespace hgnfs::gnn {

//  HypLinear 

HypLinear::HypLinear(int in_dim, int out_dim, bool use_bias)
    : in_dim_(in_dim), out_dim_(out_dim), use_bias_(use_bias) {
    // Xavier init
    float scale = std::sqrt(2.0f / (in_dim + out_dim));
    std::mt19937 rng(42);
    std::normal_distribution<float> dist(0.0f, scale);
    W_.resize(out_dim * in_dim);
    for (auto& w : W_) w = dist(rng);
    if (use_bias_) {
        b_.resize(out_dim, 0.0f);
    }
}

LorentzPoint HypLinear::forward(const LorentzPoint& x) const {
    // mobius_matvec: exp_o(W · log_o(x))
    std::vector<float> z_in(in_dim_);
    lorentz::log_o(x, z_in.data(), in_dim_);

    std::vector<float> z_out(out_dim_, 0.0f);
    for (int i = 0; i < out_dim_; ++i)
        for (int j = 0; j < in_dim_; ++j)
            z_out[i] += W_[i * in_dim_ + j] * z_in[j];

    if (use_bias_) {
        for (int i = 0; i < out_dim_; ++i)
            z_out[i] += b_[i];
    }

    LorentzPoint result = lorentz::exp_o(z_out.data(), out_dim_);
    return lorentz::project(result);
}

void HypLinear::forward_batch(std::vector<float>& X, int N, int d_out) const {
    // X: N rows of (in_dim_ + 1) Lorentz points, overwritten with (d_out + 1)
    for (int n = 0; n < N; ++n) {
        float* row = X.data() + n * (in_dim_ + 1);
        LorentzPoint x;
        x.t = row[0];
        x.x.assign(row + 1, row + 1 + in_dim_);
        auto r = forward(x);
        // In-place storage: outputs may have different dim
        // We'll handle this in the batch wrapper
    }
    // NOTE: dimension change requires reallocation - handled by caller
}

//  HypAgg 

HypAgg::HypAgg(int dim, bool use_att) : dim_(dim), use_att_(use_att) {}

LorentzPoint HypAgg::forward(const LorentzPoint& center,
                              const std::vector<LorentzPoint>& neighbors) const {
    if (neighbors.empty()) return center;

    // Aggregate in the center node's tangent space
    int d = dim_;
    std::vector<float> tan_sum(d + 1, 0.0f);
    float total_w = 0.0f;

    for (auto& nb : neighbors) {
        // log_x: map neighbor to center's tangent space
        LorentzPoint v_tan = lorentz::log_x(center, nb);
        float w = 1.0f / neighbors.size();
        for (int i = 0; i <= d; ++i) tan_sum[i] += w * (&v_tan.t)[i];
        total_w += w;
    }

    LorentzPoint v_sum;
    v_sum.t = tan_sum[0];
    v_sum.x.assign(tan_sum.begin() + 1, tan_sum.end());

    return lorentz::project(lorentz::exp_x(center, v_sum));
}

//  HypAct 

LorentzPoint hyp_relu(const LorentzPoint& x) {
    int d = static_cast<int>(x.x.size());
    std::vector<float> z(d);
    lorentz::log_o(x, z.data(), d);
    for (int i = 0; i < d; ++i) if (z[i] < 0.0f) z[i] *= 0.5f;  // leaky relu(0.5)
    return lorentz::project(lorentz::exp_o(z.data(), d));
}

void hyp_relu_batch(std::vector<float>& X, int N) {
    int D = static_cast<int>(X.size()) / N - 1;  // dim
    for (int n = 0; n < N; ++n) {
        float* row = X.data() + n * (D + 1);
        std::vector<float> z(D);
        lorentz::log_o(LorentzPoint{row[0], std::vector<float>(row+1, row+1+D)}, z.data(), D);
        for (int i = 0; i < D; ++i) if (z[i] < 0.0f) z[i] *= 0.5f;
        auto p = lorentz::project(lorentz::exp_o(z.data(), D));
        row[0] = p.t;
        std::memcpy(row + 1, p.x.data(), D * sizeof(float));
    }
}

//  HGCNLayer 

HGCNLayer::HGCNLayer(int in_dim, int out_dim, bool use_bias, bool use_att)
    : linear_(in_dim, out_dim, use_bias), agg_(out_dim, use_att) {}

LorentzPoint HGCNLayer::forward(const LorentzPoint& x,
                                 const std::vector<LorentzPoint>& neighbors) const {
    auto h = linear_.forward(x);
    auto a = agg_.forward(h, neighbors);
    return hyp_relu(a);
}

void HGCNLayer::forward_batch(const std::vector<float>& X_in, int N,
                               const std::vector<std::vector<int>>& adj,
                               std::vector<float>& X_out, int d_out) {
    int d_in = linear_.in_dim();
    int L_in = d_in + 1, L_out = d_out + 1;
    X_out.resize(N * L_out);
    const float* x_data = X_in.data();

    for (int n = 0; n < N; ++n) {
        LorentzPoint x;
        x.t = x_data[n * L_in];
        x.x.assign(x_data + n * L_in + 1, x_data + n * L_in + L_in);

        // Gather neighbors
        std::vector<LorentzPoint> nbs;
        for (int nb : adj[n]) {
            LorentzPoint nb_pt;
            nb_pt.t = x_data[nb * L_in];
            nb_pt.x.assign(x_data + nb * L_in + 1, x_data + nb * L_in + L_in);
            nbs.push_back(nb_pt);
        }
        auto r = forward(x, nbs);
        X_out[n * L_out] = r.t;
        std::memcpy(&X_out[n * L_out + 1], r.x.data(), d_out * sizeof(float));
    }
}

//  HGCNStack 

HGCNStack::HGCNStack(int in_dim, int hidden_dim, int out_dim,
                     int num_layers, bool use_att) {
    dims_.push_back(in_dim);
    for (int i = 1; i < num_layers; ++i) dims_.push_back(hidden_dim);
    dims_.push_back(out_dim);
    for (int i = 0; i < num_layers; ++i) {
        bool use_bias = (i == 0) ? true : (dims_[i] == dims_[i+1]);
        layers_.emplace_back(dims_[i], dims_[i+1], use_bias, use_att);
    }
}

void HGCNStack::forward(const std::vector<float>& X_in, int N,
                         const std::vector<std::vector<int>>& adj,
                         std::vector<float>& X_out) {
    // Lift Euclidean params to Lorentz
    int d_in = dims_[0];
    std::vector<float> X_cur(N * (d_in + 1));
    for (int n = 0; n < N; ++n) {
        auto p = lorentz::project(lorentz::exp_o(X_in.data() + n * d_in, d_in));
        X_cur[n * (d_in + 1)] = p.t;
        std::memcpy(&X_cur[n * (d_in + 1) + 1], p.x.data(), d_in * sizeof(float));
    }

    std::vector<float> X_next;
    for (auto& layer : layers_) {
        int d_out = layer.linear().out_dim();
        layer.forward_batch(X_cur, N, adj, X_next, d_out);
        std::swap(X_cur, X_next);
    }
    X_out = std::move(X_cur);
}

}  // namespace hgnfs::gnn
