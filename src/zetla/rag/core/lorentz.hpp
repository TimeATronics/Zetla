#pragma once
#include "types.hpp"
#include "numerical.hpp"
#include <cmath>
#include <vector>

namespace hgnfs::lorentz {

//  inner product / norm 

float inner(const LorentzPoint& a, const LorentzPoint& b);
float norm(const LorentzPoint& v);   // ‖v‖_L (tangent vectors)

//  geodesic distance 

float distance(const LorentzPoint& a, const LorentzPoint& b);
float squared_distance(const LorentzPoint& a, const LorentzPoint& b);

//  exponential map 

LorentzPoint exp_o(const float* z, int dim);   // Euclidean param -> Lorentz
LorentzPoint exp_x(const LorentzPoint& x, const LorentzPoint& v_tan);

//  logarithmic map 

void log_o(const LorentzPoint& x, float* z_out, int dim);  // Lorentz -> Euclidean param
LorentzPoint log_x(const LorentzPoint& x, const LorentzPoint& y);

//  parallel transport 

LorentzPoint parallel_transport_o_to_x(const LorentzPoint& v, const LorentzPoint& x);

//  project onto manifold 

LorentzPoint project(const LorentzPoint& x);
bool is_on_manifold(const LorentzPoint& x, float eps = 1e-4f);

//  batch operations 

void batch_exp_o(const float* Z, int N, int dim, float* X_out);       // X: N×(dim+1)
void batch_inner_product(const float* Q, int M, const float* X, int N,
                         int dim, float* scores);

//  Einstein midpoint 

LorentzPoint einstein_midpoint(
    const LorentzPoint* points, int n,
    const float* weights = nullptr
);

}  // namespace hgnfs::lorentz
