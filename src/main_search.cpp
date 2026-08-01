// hgnfs_search - loads exported index, runs benchmark queries
#include "hgnfs/core/lorentz.hpp"
#include "hgnfs/index/lorentz_index.hpp"
#include "hgnfs/index/loader.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace std::chrono;

struct Query {
    std::string text;
    std::vector<float> z;  // PCA-projected Euclidean param
};

std::vector<Query> load_queries(const char* path) {
    std::vector<Query> qs;
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s\n", path); return qs; }
    int dim, nq;
    fread(&dim, sizeof(int), 1, f);
    fread(&nq, sizeof(int), 1, f);
    std::vector<float> Q(nq * dim);
    fread(Q.data(), sizeof(float), nq * dim, f);
    for (int i = 0; i < nq; ++i) {
        int len; fread(&len, sizeof(int), 1, f);
        std::string txt(len, '\0');
        fread(&txt[0], 1, len, f);
        Query q; q.text = txt;
        q.z.assign(Q.data() + i * dim, Q.data() + (i + 1) * dim);
        qs.push_back(std::move(q));
    }
    fclose(f);
    return qs;
}

int main(int argc, char** argv) {
    const char* idx_path = (argc > 1) ? argv[1] : "build/index.bin";
    const char* qry_path = (argc > 2) ? argv[2] : "build/queries.bin";

    printf("hgnfs_search - Hyperbolic Retrieval Benchmark\n");
    printf("============================================\n\n");

    // Load index
    printf("Loading index: %s\n", idx_path);
    auto data = hgnfs::loader::load_binary(idx_path);
    if (data.Z.empty()) { fprintf(stderr, "FAILED\n"); return 1; }

    hgnfs::index::LorentzIndex idx(data.dim);
    idx.add_batch(data.Z.data(), (int)data.metas.size(), data.dim, data.metas);
    printf("  %d chunks | %d files | %zu KB | dim=%d\n\n",
           idx.n_chunks(), (int)data.paths.size(),
           idx.memory_bytes() / 1024, data.dim);

    // Load queries
    printf("Loading queries: %s\n", qry_path);
    auto queries = load_queries(qry_path);
    if (queries.empty()) { fprintf(stderr, "FAILED\n"); return 1; }
    printf("  %zu queries\n\n", queries.size());

    // Run
    printf("%-50s | %-45s | score   | dist   | ms\n", "Query", "Top result");
    printf("%s\n", std::string(120, '-').c_str());

    double total_ms = 0;
    for (auto& q : queries) {
        auto t0 = high_resolution_clock::now();
        auto results = idx.search(q.z.data(), data.dim, 1, nullptr);
        auto t1 = high_resolution_clock::now();
        double ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
        total_ms += ms;

        if (results.empty()) {
            printf("%-50s | %-45s | -       | -      | %.1f\n",
                   q.text.c_str(), "(no results)", ms);
        } else {
            auto& r = results[0];
            // Extract filename from path
            std::string fname = r.path;
            auto slash = fname.find_last_of("/\\");
            if (slash != std::string::npos) fname = fname.substr(slash + 1);
            printf("%-50s | %-45s | %+.4f | %.4f | %.1f\n",
                   q.text.c_str(), fname.c_str(), r.score, r.distance, ms);
        }
    }
    printf("\n  Avg: %.1f ms/query | %d chunks | %zu KB\n",
           total_ms / queries.size(), idx.n_chunks(), idx.memory_bytes() / 1024);

    return 0;
}
