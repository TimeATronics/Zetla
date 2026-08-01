#include "rag_tool.hpp"
#include <cstdio>
#include <chrono>
int main() {
    auto& mgr = zetla::rag::RagManager::instance();
    if (!mgr.init_embedder("Zetla/data/src/main/assets/bge_model")) { printf("init fail\n"); return 1; }
    mgr.set_projection_enabled(false);
    
    // Test 100 embeddings
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100; i++) {
        std::string text = "The quick brown fox jumps over the lazy dog. This is a test sentence for performance measurement. " + std::to_string(i);
        auto json = mgr.embed_for_test(text);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    printf("100 embeds: %lld ms (%.1f ms/embed)\n", ms, ms / 100.0);
    return 0;
}
