#pragma once
#include "../core/types.hpp"
#include "../core/lorentz.hpp"
#include <vector>
#include <functional>

namespace hgnfs::gnn {

//  Hyperbolic Linear layer 

class HypLinear {
public:
    HypLinear(int in_dim, int out_dim, bool use_bias = true);
    LorentzPoint forward(const LorentzPoint& x) const;

    // In-place batch transform: x is (N, in_dim+1), modified in-place
    void forward_batch(std::vector<float>& X, int N, int d_out) const;

    int in_dim() const { return in_dim_; }
    int out_dim() const { return out_dim_; }
    const float* weight_data() const { return W_.data(); }
    const float* bias_data() const { return b_.data(); }
    float* weight_data() { return W_.data(); }
    float* bias_data() { return b_.data(); }

private:
    int in_dim_, out_dim_;
    std::vector<float> W_;   // out_dim × in_dim
    std::vector<float> b_;   // out_dim
    bool use_bias_;
};

//  Hyperbolic Aggregation layer 

class HypAgg {
public:
    HypAgg(int dim, bool use_att = false);
    LorentzPoint forward(const LorentzPoint& center,
                         const std::vector<LorentzPoint>& neighbors) const;

private:
    int dim_;
    bool use_att_;
};

//  Hyperbolic Activation 

LorentzPoint hyp_relu(const LorentzPoint& x);
void hyp_relu_batch(std::vector<float>& X, int N);

//  Full HGCN Layer 

class HGCNLayer {
public:
    HGCNLayer(int in_dim, int out_dim, bool use_bias = true, bool use_att = false);
    LorentzPoint forward(const LorentzPoint& x,
                         const std::vector<LorentzPoint>& neighbors) const;
    void forward_batch(const std::vector<float>& X_in, int N,
                       const std::vector<std::vector<int>>& adj,
                       std::vector<float>& X_out, int d_out);

    HypLinear& linear() { return linear_; }

private:
    HypLinear linear_;
    HypAgg agg_;
};

//  HGCN Stack 

class HGCNStack {
public:
    HGCNStack(int in_dim, int hidden_dim, int out_dim,
              int num_layers = 2, bool use_att = false);

    // Forward pass on a graph
    void forward(const std::vector<float>& X_in, int N,
                 const std::vector<std::vector<int>>& adj,
                 std::vector<float>& X_out);

    // Access layers for training
    std::vector<HGCNLayer>& layers() { return layers_; }
    const std::vector<HGCNLayer>& layers() const { return layers_; }

private:
    std::vector<HGCNLayer> layers_;
    std::vector<int> dims_;
};

}  // namespace hgnfs::gnn
