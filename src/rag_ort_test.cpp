#include "rag_tool.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <chrono>
#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    const char* model_dir = "Zetla\\Zetla\\data\\src\\main\\assets\\bge_model";
    const char* txt_dir = "test\\txt";

    printf("=== RAG Pipeline Test ===\n\n");

    auto& mgr = zetla::rag::RagManager::instance();

    // Init embedder
    auto t0 = std::chrono::high_resolution_clock::now();
    if (!mgr.init_embedder(model_dir)) {
        printf("FAIL: init_embedder\n");
        return 1;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    printf("Embedder loaded in %.0f ms\n\n",
           std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() * 1.0);

    mgr.create_session("test");

    // Index all .txt files
    printf("=== Indexing Files ===\n");
    for (auto& entry : fs::directory_iterator(txt_dir)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        if (path.extension() != ".txt") continue;

        std::string text = read_file(path.string());
        if (text.empty()) continue;

        auto t2 = std::chrono::high_resolution_clock::now();
        int n = mgr.add_file("test", path.filename().string(), text);
        auto t3 = std::chrono::high_resolution_clock::now();

        printf("  %s: %d chunks in %.0f ms\n",
               path.filename().string().c_str(), n,
               std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count() * 1.0);
    }

    // Build graph + enhance embeddings
    printf("\nEnhancing with hyperbolic graph...\n");
    auto tg0 = std::chrono::high_resolution_clock::now();
    mgr.enhance_with_graph("test", 5);
    auto tg1 = std::chrono::high_resolution_clock::now();
    printf("  Graph enhancement: %.0f ms\n",
           std::chrono::duration_cast<std::chrono::milliseconds>(tg1 - tg0).count() * 1.0);

    printf("\nTotal chunks: %d, memory: %zu KB\n\n",
           mgr.chunk_count("test"), mgr.memory_bytes("test") / 1024);

    // Search
    const char* queries[] = {
        "cellular automata",
        "fake news viral diffusion",
        "A5/1 cipher hardware model",
        "deepfake detector",
        "death",
    };

    printf("=== Search Results ===\n");
    for (auto q : queries) {
        auto t4 = std::chrono::high_resolution_clock::now();
        auto json = mgr.search("test", q, 5, "");
        auto t5 = std::chrono::high_resolution_clock::now();

        printf("\nQuery: \"%s\" (%.1f ms)\n", q,
               std::chrono::duration_cast<std::chrono::milliseconds>(t5 - t4).count() * 1.0);

        // Parse and display
        try {
            auto j = nlohmann::json::parse(json);
            int i = 1;
            for (auto& item : j) {
                std::string path = item.value("path", "");
                float score = item.value("score", 0.0f);
                std::string text = item.value("text", "");
                // Truncate path to just filename
                auto slash = path.find_last_of("/\\");
                if (slash != std::string::npos) path = path.substr(slash + 1);
                printf("  [%d] %s (s=%.3f): %.80s...\n",
                       i++, path.c_str(), score, text.c_str());
            }
        } catch (...) {
            printf("  %s\n", json.c_str());
        }
    }

    mgr.shutdown();
    return 0;
}
