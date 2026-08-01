#pragma once
#include <cstdint>

namespace hgnfs::gpu {

struct VulkanContext;

VulkanContext* vk_create();
void vk_destroy(VulkanContext* ctx);

// Upload Z[N*dim] -> GPU, compute exp_o, download to X_out[N*(dim+1)]
// Falls back to CPU if Vulkan unavailable.
void vk_batch_exp_o(VulkanContext* ctx, const float* Z, int N, int dim, float* X_out);

// Upload Q[M*L] × X[N*L] -> GPU, compute Lorentz inner products, download scores[M*N]
void vk_batch_inner_product(VulkanContext* ctx,
    const float* Q, int M, const float* X, int N, int dim, float* scores);

bool vk_is_available();

}  // namespace hgnfs::gpu
