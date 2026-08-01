// Vulkan compute backend - links against libvulkan-1.dll.a (generated from System32 vulkan-1.dll)
// SPIR-V embedded from spirv_data.h
#include "vulkan_context.hpp"
#include "spirv_data.h"
#include "../core/lorentz.hpp"
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <vector>

//  helpers 

static VkInstance   g_inst = VK_NULL_HANDLE;
static VkDevice     g_dev  = VK_NULL_HANDLE;
static VkPhysicalDevice g_phys = VK_NULL_HANDLE;

static uint32_t find_mem_type(uint32_t bits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties((VkPhysicalDevice)g_phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return 0;
}

namespace hgnfs::gpu {

struct VulkanContext {
    VkQueue queue;
    VkCommandPool cmd_pool;
    VkDescriptorPool desc_pool;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkPipeline pipeline_exp;
    VkPipeline pipeline_dot;
    uint32_t qf;
};

bool vk_is_available() {
    // Try creating an instance - if this fails, Vulkan isn't available
    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;
    VkInstance tmp;
    if (vkCreateInstance(&ci, nullptr, &tmp) != VK_SUCCESS) return false;
    vkDestroyInstance(tmp, nullptr);
    return true;
}

VulkanContext* vk_create() {
    if (g_inst) return nullptr; // already created

    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "hgnfs";
    ai.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;

    VkResult rc = vkCreateInstance(&ci, nullptr, &g_inst);
    if (rc != VK_SUCCESS || !g_inst) {
        fprintf(stderr, "Vulkan: vkCreateInstance failed\n"); return nullptr;
    }

    // Pick first device
    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(g_inst, &nd, nullptr);
    if (!nd) { vkDestroyInstance(g_inst, nullptr); g_inst = nullptr; return nullptr; }
    std::vector<VkPhysicalDevice> devs(nd);
    vkEnumeratePhysicalDevices(g_inst, &nd, devs.data());
    g_phys = devs[0];
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(g_phys, &props);
    printf("Vulkan: %s\n", props.deviceName);

    // Find compute queue
    uint32_t qfc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_phys, &qfc, nullptr);
    std::vector<VkQueueFamilyProperties> qps(qfc);
    vkGetPhysicalDeviceQueueFamilyProperties(g_phys, &qfc, qps.data());
    uint32_t qf = 0;
    for (uint32_t i = 0; i < qfc; ++i)
        if (qps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { qf = i; break; }

    // Create device
    float prio = 1.0f;
    VkDeviceQueueCreateInfo dqci{};
    dqci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dqci.queueFamilyIndex = qf; dqci.queueCount = 1; dqci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &dqci;
    vkCreateDevice(g_phys, &dci, nullptr, &g_dev);
    if (!g_dev) { vkDestroyInstance(g_inst, nullptr); return nullptr; }

    auto* ctx = new VulkanContext;
    ctx->qf = qf;
    vkGetDeviceQueue(g_dev, qf, 0, &ctx->queue);

    // Command pool
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = qf;
    vkCreateCommandPool(g_dev, &cpci, nullptr, &ctx->cmd_pool);

    // Descriptor pool
    VkDescriptorPoolSize dps{};
    dps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; dps.descriptorCount = 16;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 8; dpci.poolSizeCount = 1; dpci.pPoolSizes = &dps;
    vkCreateDescriptorPool(g_dev, &dpci, nullptr, &ctx->desc_pool);

    // Descriptor set layout (3 storage buffers: in1, in2, out)
    VkDescriptorSetLayoutBinding bindings[3] = {};
    for (int i = 0; i < 3; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dsci{};
    dsci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsci.bindingCount = 3; dsci.pBindings = bindings;
    vkCreateDescriptorSetLayout(g_dev, &dsci, nullptr, &ctx->dsl);

    // Pipeline layout
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; pcr.size = 16;
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1; plci.pSetLayouts = &ctx->dsl;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(g_dev, &plci, nullptr, &ctx->layout);

    // Helper: create compute pipeline from SPIR-V
    auto mk_pipe = [&](const uint32_t* spv, size_t sz, VkPipeline& pipe) {
        VkShaderModuleCreateInfo smci{};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = sz; smci.pCode = spv;
        VkShaderModule sm;
        vkCreateShaderModule(g_dev, &smci, nullptr, &sm);
        VkPipelineShaderStageCreateInfo ssci{};
        ssci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ssci.stage = VK_SHADER_STAGE_COMPUTE_BIT; ssci.module = sm; ssci.pName = "main";
        VkComputePipelineCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage = ssci; cpci.layout = ctx->layout;
        vkCreateComputePipelines(g_dev, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipe);
        vkDestroyShaderModule(g_dev, sm, nullptr);
    };
    mk_pipe(SPIRV_EXP_O, SPIRV_EXP_O_SIZE, ctx->pipeline_exp);
    mk_pipe(SPIRV_INNER_PRODUCT, SPIRV_INNER_PRODUCT_SIZE, ctx->pipeline_dot);

    return ctx;
}

void vk_destroy(VulkanContext* ctx) {
    if (!ctx) return;
    if (g_dev) {
        vkDestroyPipeline(g_dev, ctx->pipeline_exp, nullptr);
        vkDestroyPipeline(g_dev, ctx->pipeline_dot, nullptr);
        vkDestroyPipelineLayout(g_dev, ctx->layout, nullptr);
        vkDestroyDescriptorSetLayout(g_dev, ctx->dsl, nullptr);
        vkDestroyCommandPool(g_dev, ctx->cmd_pool, nullptr);
        vkDestroyDescriptorPool(g_dev, ctx->desc_pool, nullptr);
        vkDestroyDevice(g_dev, nullptr); g_dev = VK_NULL_HANDLE;
    }
    if (g_inst) { vkDestroyInstance(g_inst, nullptr); g_inst = VK_NULL_HANDLE; }
    delete ctx;
}

//  buffer helpers 

struct GpuBuf { VkBuffer buf; VkDeviceMemory mem; void* mapped; VkDeviceSize size; };

static GpuBuf create_buf(VkDeviceSize size) {
    GpuBuf b{}; b.size = size;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size; bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    vkCreateBuffer(g_dev, &bci, nullptr, &b.buf);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_dev, b.buf, &req);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = find_mem_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(g_dev, &mai, nullptr, &b.mem);
    vkBindBufferMemory(g_dev, b.buf, b.mem, 0);
    vkMapMemory(g_dev, b.mem, 0, size, 0, &b.mapped);
    return b;
}

static void destroy_buf(GpuBuf& b) {
    if (b.mapped) vkUnmapMemory(g_dev, b.mem);
    if (b.mem) vkFreeMemory(g_dev, b.mem, nullptr);
    if (b.buf) vkDestroyBuffer(g_dev, b.buf, nullptr);
}

//  dispatch 

static VkCommandBuffer begin_cb(VulkanContext* ctx) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = ctx->cmd_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(g_dev, &ai, &cb);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    return cb;
}

static void submit_wait(VulkanContext* ctx, VkCommandBuffer cb) {
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(ctx->queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->queue);
    vkFreeCommandBuffers(g_dev, ctx->cmd_pool, 1, &cb);
}

//  public API 

void vk_batch_exp_o(VulkanContext* ctx, const float* Z, int N, int dim, float* X_out) {
    if (!ctx || !g_dev) { lorentz::batch_exp_o(Z, N, dim, X_out); return; }
    // Alloc, upload, dispatch, download
    auto in  = create_buf(N * dim * sizeof(float));
    auto out = create_buf(N * (dim + 1) * sizeof(float));
    memcpy(in.mapped, Z, in.size);

    // Descriptor set
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = ctx->desc_pool; dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &ctx->dsl;
    VkDescriptorSet ds;
    vkAllocateDescriptorSets(g_dev, &dsai, &ds);

    VkDescriptorBufferInfo dbi_in  = { in.buf,  0, in.size };
    VkDescriptorBufferInfo dbi_out = { out.buf, 0, out.size };
    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = ds; writes[0].dstBinding = 0; writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[0].pBufferInfo = &dbi_in;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = ds; writes[1].dstBinding = 1; writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].pBufferInfo = &dbi_out;
    vkUpdateDescriptorSets(g_dev, 2, writes, 0, nullptr);

    int pc[4] = { N, dim, 0, 0 };
    auto cb = begin_cb(ctx);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->pipeline_exp);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->layout, 0, 1, &ds, 0, nullptr);
    vkCmdPushConstants(cb, ctx->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
    vkCmdDispatch(cb, (N + 255) / 256, 1, 1);
    submit_wait(ctx, cb);

    memcpy(X_out, out.mapped, out.size);
    destroy_buf(in); destroy_buf(out);
}

void vk_batch_inner_product(VulkanContext* ctx,
    const float* Q, int M, const float* X, int N, int dim, float* scores) {
    if (!ctx || !g_dev) {
        lorentz::batch_inner_product(Q, M, X, N, dim, scores); return;
    }
    int L = dim + 1;
    auto inQ = create_buf(M * L * sizeof(float));
    auto inX = create_buf(N * L * sizeof(float));
    auto outS = create_buf(M * N * sizeof(float));
    memcpy(inQ.mapped, Q, inQ.size);
    memcpy(inX.mapped, X, inX.size);

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = ctx->desc_pool; dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &ctx->dsl;
    VkDescriptorSet ds;
    vkAllocateDescriptorSets(g_dev, &dsai, &ds);

    VkDescriptorBufferInfo dbiQ = { inQ.buf, 0, inQ.size };
    VkDescriptorBufferInfo dbiX = { inX.buf, 0, inX.size };
    VkDescriptorBufferInfo dbiS = { outS.buf, 0, outS.size };
    VkWriteDescriptorSet writes[3] = {};
    for (int i = 0; i < 3; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = ds; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    writes[0].pBufferInfo = &dbiQ; writes[1].pBufferInfo = &dbiX; writes[2].pBufferInfo = &dbiS;
    vkUpdateDescriptorSets(g_dev, 3, writes, 0, nullptr);

    int pc[4] = { M, N, dim, 0 };
    auto cb = begin_cb(ctx);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->pipeline_dot);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->layout, 0, 1, &ds, 0, nullptr);
    vkCmdPushConstants(cb, ctx->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
    vkCmdDispatch(cb, (M + 15) / 16, (N + 15) / 16, 1);
    submit_wait(ctx, cb);

    memcpy(scores, outS.mapped, outS.size);
    destroy_buf(inQ); destroy_buf(inX); destroy_buf(outS);
}

}  // namespace hgnfs::gpu
