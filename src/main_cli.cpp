// hgnfs_cli - test harness for the hyperbolic retrieval DLL
#include "hgnfs/api/hgnfs_api.h"
#include "hgnfs/core/lorentz.hpp"
#include "hgnfs/core/types.hpp"
#include "hgnfs/index/lorentz_index.hpp"
#include "hgnfs/chunker/chunker.hpp"
#include "hgnfs/projector/pca.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <iostream>
#include <string>

using hgnfs::LorentzPoint;
using namespace std::chrono;

//  helpers 

void print_response(const char* label, const hgnfs_response& r) {
    if (r.success == HGNFS_OK) {
        printf("  %-20s OK: %s\n", label, r.data ? r.data : "null");
    } else {
        printf("  %-20s ERR %d: %s\n", label, r.success, r.error ? r.error : "unknown");
    }
}

void fill_random(float* z, int dim, float scale = 0.5f) {
    for (int i = 0; i < dim; ++i) z[i] = ((float)rand()/RAND_MAX * 2.0f - 1.0f) * scale;
}

//  test 1: Lorentz math identity tests 

void test_lorentz_math() {
    printf("\n=== Test 1: Lorentz Math ===\n");

    int dim = 16;
    float z[16];
    fill_random(z, dim, 1.0f);

    // exp_o -> log_o roundtrip
    LorentzPoint x = hgnfs::lorentz::exp_o(z, dim);
    float z_recovered[16];
    hgnfs::lorentz::log_o(x, z_recovered, dim);

    float err = 0.0f;
    for (int i = 0; i < dim; ++i) {
        float d = z[i] - z_recovered[i];
        err += d * d;
    }
    printf("  exp_o / log_o roundtrip: rms err = %.6f\n", std::sqrt(err / dim));

    // Manifold check
    bool on = hgnfs::lorentz::is_on_manifold(x, 1e-4f);
    printf("  is_on_manifold: %s\n", on ? "PASS" : "FAIL");

    // Self-distance = 0
    float d0 = hgnfs::lorentz::distance(x, x);
    printf("  distance(x, x) = %.6f %s\n", d0, d0 < 0.01f ? "PASS" : "FAIL");

    // Symmetry
    float z2[16];
    fill_random(z2, dim, 1.0f);
    LorentzPoint x2 = hgnfs::lorentz::exp_o(z2, dim);
    float d12 = hgnfs::lorentz::distance(x, x2);
    float d21 = hgnfs::lorentz::distance(x2, x);
    printf("  distance symmetric: %.4f vs %.4f %s\n", d12, d21,
           std::abs(d12 - d21) < 1e-4f ? "PASS" : "FAIL");

    // Batch ops
    int N = 100;
    std::vector<float> Z_batch(N * dim);
    for (int i = 0; i < N * dim; ++i) Z_batch[i] = ((float)rand()/RAND_MAX - 0.5f);
    std::vector<float> X_batch(N * (dim + 1));
    hgnfs::lorentz::batch_exp_o(Z_batch.data(), N, dim, X_batch.data());

    std::vector<float> scores(N);
    float q[17] = {0};
    q[0] = -1.0f; // negate time of query for inner product
    hgnfs::lorentz::batch_inner_product(q, 1, X_batch.data(), N, dim, scores.data());
    printf("  batch_inner_product (N=%d): first score = %.4f\n", N, scores[0]);

    // Einstein midpoint
    std::vector<LorentzPoint> pts;
    for (int i = 0; i < 5; ++i) pts.push_back(hgnfs::lorentz::exp_o(Z_batch.data() + i * dim, dim));
    auto mid = hgnfs::lorentz::einstein_midpoint(pts.data(), (int)pts.size(), nullptr);
    printf("  einstein_midpoint (5 pts): on_manifold = %s\n",
           hgnfs::lorentz::is_on_manifold(mid, 1e-3f) ? "PASS" : "FAIL");
}

//  test 2: Index + search 

void test_index_search() {
    printf("\n=== Test 2: Index + Search ===\n");

    int dim = 16;
    hgnfs::index::LorentzIndex idx(dim);

    // Add 50 chunks
    for (int i = 0; i < 50; ++i) {
        float z[16];
        fill_random(z, dim, 0.5f);
        idx.add(z, dim, {"test/file.txt", i});
    }

    // Search
    float query_z[16];
    fill_random(query_z, dim, 1.0f);

    auto t0 = high_resolution_clock::now();
    auto results = idx.search(query_z, dim, 10, nullptr);
    auto t1 = high_resolution_clock::now();
    auto us = duration_cast<microseconds>(t1 - t0).count();

    printf("  Index: %d chunks, %zu KB\n", idx.n_chunks(), idx.memory_bytes() / 1024);
    printf("  Search: %zu results in %.1f ms\n", results.size(), us / 1000.0);

    // Scoped search
    results = idx.search(query_z, dim, 5, "test/other.txt");
    printf("  Scoped (wrong path): %zu results (expected 0)\n", results.size());

    // Valid chunk search
    idx.add(query_z, dim, {"test/auth.py", 0});
    results = idx.search(query_z, dim, 3, "test/auth.py");
    printf("  Scoped (correct path): %zu results (expected 1)\n", results.size());
}

//  test 3: SQLite persistence 

void test_storage() {
    printf("\n=== Test 3: SQLite Persistence ===\n");

    const char* db_path = "build/test_hgnfs.db";
    remove(db_path);  // clean slate

    hgnfs_init(db_path);

    int dim = 16;
    float z[16];
    fill_random(z, dim);
    auto resp = hgnfs_set_file_embedding("/docs/report.pdf", z, dim);
    print_response("set embedding", resp);
    hgnfs_free_response(&resp);

    resp = hgnfs_get_file_embedding("/docs/report.pdf", dim);
    print_response("get embedding", resp);
    hgnfs_free_response(&resp);

    // Add chunks via API
    for (int i = 0; i < 10; ++i) {
        float cz[16];
        fill_random(cz, dim);
        resp = hgnfs_add_chunk("/docs/report.pdf", i, cz, dim);
        hgnfs_free_response(&resp);
    }
    printf("  chunks: %d, memory: %zu KB\n", hgnfs_n_chunks(), hgnfs_memory_bytes() / 1024);

    // Search via API
    float qz[16];
    fill_random(qz, dim);
    resp = hgnfs_search(qz, dim, 5, nullptr);
    print_response("search", resp);
    hgnfs_free_response(&resp);

    hgnfs_shutdown();
    remove(db_path);
}

//  test 4: PCA projection 

void test_pca() {
    printf("\n=== Test 4: PCA Projection ===\n");

    int n = 200, input_dim = 64, target_dim = 16;
    std::vector<float> data(n * input_dim);
    for (int i = 0; i < n * input_dim; ++i)
        data[i] = (float)rand() / RAND_MAX;

    hgnfs::projector::PCAProjector pca;
    auto t0 = high_resolution_clock::now();
    pca.fit(data.data(), n, input_dim, target_dim);
    auto t1 = high_resolution_clock::now();
    auto us = duration_cast<microseconds>(t1 - t0).count();

    printf("  PCA fit: %d -> %d in %.1f ms\n", input_dim, target_dim, us / 1000.0);

    std::vector<float> out(n * target_dim);
    pca.transform_batch(data.data(), n, input_dim, out.data(), target_dim);

    // Verify norm preservation
    float sum_in = 0, sum_out = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < input_dim; ++j) sum_in += data[i * input_dim + j] * data[i * input_dim + j];
        for (int j = 0; j < target_dim; ++j) sum_out += out[i * target_dim + j] * out[i * target_dim + j];
    }
    printf("  PCA variance ratio: %.2f %%\n", 100.0 * sum_out / sum_in);
}

//  test 5: Chunking 

void test_chunking() {
    printf("\n=== Test 5: Text Chunking ===\n");

    std::string text;
    for (int i = 0; i < 1000; ++i) {
        text += "word" + std::to_string(i) + " ";
    }

    auto chunks = hgnfs::chunker::chunk_text(text, 200, 50);
    printf("  Input: 1000 words -> %zu chunks\n", chunks.size());
    if (!chunks.empty()) {
        printf("  Chunk 0 words: %zu\n",
               std::count(chunks[0].text.begin(), chunks[0].text.end(), ' ') + 1);
        printf("  Chunk 1 words: %zu\n",
               std::count(chunks[1].text.begin(), chunks[1].text.end(), ' ') + 1);
    }
}

//  test 6: RAG pipeline integration (chunker -> embed -> PCA -> index -> search) 

static std::vector<float> stub_embed(const std::string& text, int dim = 384) {
    std::vector<float> vec(dim, 0.0f);
    size_t h = std::hash<std::string>{}(text);
    for (int i = 0; i < dim; ++i) {
        h = h * 1103515245 + 12345;
        vec[i] = ((float)(h & 0xFFFF) / 65535.0f - 0.5f) * 0.2f;
    }
    float n2 = 0.0f;
    for (float v : vec) n2 += v * v;
    float n = std::sqrt(n2);
    if (n > 0.0f) for (float& v : vec) v /= n;
    return vec;
}

void test_rag_pipeline() {
    printf("\n=== Test 6: RAG Pipeline Integration ===\n");

    //  1. Create sample documents 
    std::vector<std::pair<std::string, std::string>> docs = {
        {"docs/report.txt",  "This quarterly financial report shows revenue growth of 15% "
                             "driven by strong sales in the APAC region. Operating expenses "
                             "increased 8% due to investments in R&D and marketing."},
        {"docs/manual.txt",  "To install the device, first connect the power cable to the "
                             "back panel. Then press and hold the power button for 3 seconds. "
                             "The LED indicator will flash blue during startup."},
        {"docs/notes.txt",   "Meeting notes: discussed Q3 priorities. Action items: finalize "
                             "budget by Friday, schedule follow-up with engineering team, "
                             "prepare slides for board presentation."},
        {"docs/readme.txt",  "This project is licensed under the MIT license. See LICENSE.md "
                             "for details. Dependencies: Python 3.10+, PyTorch 2.0+."},
    };

    int target_dim = 16;
    hgnfs::index::LorentzIndex idx(target_dim);
    hgnfs::projector::PCAProjector pca;
    std::vector<std::string> all_chunks;
    bool pca_fitted = false;
    std::vector<std::vector<float>> all_embeds_384;

    int total_chunks = 0;

    //  2. Chunk + embed each document 
    for (auto& [path, text] : docs) {
        auto chunks = hgnfs::chunker::chunk_text(text, 100, 20);
        printf("  %s: %zu chunks\n", path.c_str(), chunks.size());

        int chunk_offset = (int)all_chunks.size();
        for (auto& c : chunks) {
            all_chunks.push_back(c.text);
            auto euc = stub_embed(c.text, 384);
            if (!pca_fitted) all_embeds_384.push_back(euc);

            hgnfs::ChunkMeta meta{path, chunk_offset++};
            (void)meta;
        }
        total_chunks += (int)chunks.size();
    }

    //  3. Fit PCA 
    if (all_embeds_384.size() >= 3) {
        int n = (int)all_embeds_384.size();
        std::vector<float> data(n * 384);
        for (int i = 0; i < n; ++i)
            std::memcpy(&data[i * 384], all_embeds_384[i].data(), 384 * sizeof(float));
        pca.fit(data.data(), n, 384, target_dim);
        pca_fitted = true;
        printf("  PCA fitted: %d samples -> %dD\n", n, target_dim);
    }

    //  4. Project + index all chunks 
    int indexed = 0;
    for (int ci = 0; ci < total_chunks; ++ci) {
        auto& euc = all_embeds_384[ci];
        std::vector<float> z(target_dim);
        if (pca_fitted) {
            pca.transform(euc.data(), 384, z.data(), target_dim);
        } else {
            for (int i = 0; i < target_dim && i < 384; ++i) z[i] = euc[i];
        }
        // Clamp norm to 3.0
        float n2 = 0.0f;
        for (float v : z) n2 += v * v;
        float n = std::sqrt(n2);
        if (n > 3.0f) for (float& v : z) v *= 3.0f / n;

        // Find which doc this chunk belongs to
        std::string chunk_path;
        int running = 0;
        for (auto& [path, text] : docs) {
            auto ch = hgnfs::chunker::chunk_text(text, 100, 20);
            if (ci < running + (int)ch.size()) {
                chunk_path = path;
                break;
            }
            running += (int)ch.size();
        }
        int chunk_idx_in_doc = ci - running;
        (void)chunk_idx_in_doc;
        // Recompute: running = sum of chunks before current doc
        running = 0;
        for (auto& [path, text] : docs) {
            auto ch = hgnfs::chunker::chunk_text(text, 100, 20);
            if (ci < running + (int)ch.size()) {
                chunk_path = path;
                break;
            }
            running += (int)ch.size();
        }
        idx.add(z.data(), target_dim, {chunk_path, ci - running});
        indexed++;
    }
    printf("  Indexed: %d chunks in LorentzIndex (dim=%d)\n", indexed, target_dim);

    //  5. Search queries 

    struct QueryTest {
        const char* query_text;
        const char* expected_keyword;
        const char* scope;
    };
    QueryTest queries[] = {
        {"financial report revenue growth", "revenue", nullptr},
        {"how to install the device", "install", nullptr},
        {"Q3 priorities action items", "Q3", nullptr},
        {"project license details", "MIT", nullptr},
        {"search in manual only", "install", "docs/manual.txt"},
    };
    int n_queries = sizeof(queries) / sizeof(queries[0]);

    for (int qi = 0; qi < n_queries; ++qi) {
        auto& qt = queries[qi];
        auto q_euc = stub_embed(qt.query_text, 384);
        std::vector<float> q_z(target_dim);
        if (pca_fitted) {
            pca.transform(q_euc.data(), 384, q_z.data(), target_dim);
        } else {
            for (int i = 0; i < target_dim && i < 384; ++i) q_z[i] = q_euc[i];
        }

        auto results = idx.search(q_z.data(), target_dim, 3,
                                  qt.scope && qt.scope[0] ? qt.scope : nullptr);

        printf("  Query: \"%s\"", qt.query_text);
        if (qt.scope) printf(" [scope=%s]", qt.scope);
        printf(" -> %zu results\n", results.size());

        bool found_keyword = false;
        for (auto& r : results) {
            // Get the chunk text by calculating its index in all_chunks
            int running = 0;
            int chunk_global_idx = -1;
            for (auto& [path, text] : docs) {
                auto ch = hgnfs::chunker::chunk_text(text, 100, 20);
                int n_ch = (int)ch.size();
                if (r.path == path && r.chunk_idx < n_ch) {
                    chunk_global_idx = running + r.chunk_idx;
                    break;
                }
                running += n_ch;
            }
            const char* snippet = (chunk_global_idx >= 0 && chunk_global_idx < (int)all_chunks.size())
                ? all_chunks[chunk_global_idx].c_str() : "(unknown)";
            if (strstr(snippet, qt.expected_keyword)) found_keyword = true;
            printf("    [%s #%d] score=%.4f dist=%.4f: %.60s...\n",
                   r.path.c_str(), r.chunk_idx, r.score, r.distance, snippet);
        }

        printf("  %s keyword '%s' in top-3\n",
               found_keyword ? "PASS: found" : "FAIL: missing", qt.expected_keyword);
    }

    //  6. Stats 
    printf("  Final stats: %d chunks, %zu KB memory\n",
           idx.n_chunks(), idx.memory_bytes() / 1024);
}

//  main 

int main() {
    printf("hgnfs_cli - Hyperbolic Retrieval Test Suite\n");
    printf("==========================================\n");

    test_lorentz_math();
    test_index_search();
    test_storage();
    test_pca();
    test_chunking();
    test_rag_pipeline();

    printf("\nAll tests complete.\n");
    return 0;
}
