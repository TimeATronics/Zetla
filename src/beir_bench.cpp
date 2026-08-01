#include "rag_tool.hpp"
#include "bm25_index.hpp"
#include "hyp_embedder.hpp"
#include "bert_tokenizer.hpp"
#include "index/lorentz_index.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>

struct DocEntry { std::string id, title, text; };
struct QueryEntry { std::string id, query; };
using Qrels = std::unordered_map<std::string, std::unordered_map<std::string, int>>;

static Qrels load_qrels(const std::string& path) {
    Qrels q;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string qid, did; int label = 0;
        std::getline(ss, qid, '\t');
        std::getline(ss, did, '\t');
        ss >> label;
        if (label > 0) q[qid][did] = label;
    }
    return q;
}

static double dcg_at(const std::vector<std::string>& ranked, const Qrels& q, 
                      const std::string& qid, int k) {
    auto it = q.find(qid);
    if (it == q.end()) return 0.0;
    double d = 0.0;
    for (int i = 0; i < k && i < (int)ranked.size(); ++i) {
        auto rit = it->second.find(ranked[i]);
        if (rit != it->second.end()) d += rit->second / std::log2(i + 2.0);
    }
    return d;
}

static double idcg_at(const Qrels& q, const std::string& qid, int k) {
    auto it = q.find(qid);
    if (it == q.end()) return 0.0;
    std::vector<int> rels;
    for (auto& [did, r] : it->second) rels.push_back(r);
    std::sort(rels.begin(), rels.end(), std::greater<int>());
    double d = 0.0;
    for (int i = 0; i < k && i < (int)rels.size(); ++i)
        d += rels[i] / std::log2(i + 2.0);
    return d;
}

static double ndcg_at_k(const std::vector<std::string>& ranked, const Qrels& q,
                         const std::string& qid, int k) {
    double d = dcg_at(ranked, q, qid, k);
    double id = idcg_at(q, qid, k);
    return (id > 0.0) ? d / id : 0.0;
}

static double map_at_k(const std::vector<std::string>& ranked, const Qrels& q,
                        const std::string& qid, int k) {
    auto it = q.find(qid);
    if (it == q.end() || it->second.empty()) return 0.0;
    int hits = 0;
    double ap = 0.0;
    for (int i = 0; i < k && i < (int)ranked.size(); ++i) {
        if (it->second.count(ranked[i])) { hits++; ap += (double)hits / (i + 1.0); }
    }
    if (hits == 0) return 0.0;
    return ap / std::min((int)it->second.size(), k);
}

static double recall_at_k(const std::vector<std::string>& ranked, const Qrels& q,
                           const std::string& qid, int k) {
    auto it = q.find(qid);
    if (it == q.end() || it->second.empty()) return 0.0;
    int total = (int)it->second.size();
    int found = 0;
    for (int i = 0; i < k && i < (int)ranked.size(); ++i) {
        if (it->second.count(ranked[i])) found++;
    }
    return (double)found / total;
}

// Split markdown text into sections (## headers), return section name + text
static std::vector<std::pair<std::string, std::string>> split_sections(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> secs;
    std::string current_section;
    std::string current_text;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.rfind("## ", 0) == 0 || line.rfind("# ", 0) == 0) {
            if (!current_text.empty()) {
                secs.emplace_back(current_section, current_text);
            }
            current_section = line.substr(line.find(' ') + 1);
            current_text.clear();
        } else {
            if (!current_text.empty()) current_text += "\n";
            current_text += line;
        }
    }
    if (!current_text.empty()) secs.emplace_back(current_section, current_text);
    // If no sections found, treat whole text as one
    if (secs.empty()) secs.emplace_back("", text);
    return secs;
}

struct RunResult {
    double ndcg10 = 0, map100 = 0, recall100 = 0;
    double idx_ms = 0, query_ms = 0;
    int chunks = 0;
};

static RunResult run_bench(const std::string& data_dir, const std::string& model_dir,
                            float alpha_bm25, bool use_rerank, bool use_sections) {
    fprintf(stderr, "\n=== a=%.1f rerank=%d sections=%d ===\n", alpha_bm25, (int)use_rerank, (int)use_sections);
    fflush(stderr);

    std::vector<DocEntry> docs;
    {
        std::ifstream f(data_dir + "/corpus.tsv");
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            std::istringstream ss(line);
            DocEntry d;
            std::getline(ss, d.id, '\t');
            std::getline(ss, d.title, '\t');
            std::getline(ss, d.text);
            docs.push_back(d);
        }
    }
    std::vector<QueryEntry> queries;
    {
        std::ifstream f(data_dir + "/queries.tsv");
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            std::istringstream ss(line);
            QueryEntry q;
            std::getline(ss, q.id, '\t');
            std::getline(ss, q.query);
            queries.push_back(q);
        }
    }
    auto qrels = load_qrels(data_dir + "/qrels.tsv");
    fprintf(stderr, "Docs: %zu  Queries: %zu  Qrels: %zu\n", docs.size(), queries.size(), qrels.size());

    // BM25
    fprintf(stderr, "BM25... "); fflush(stderr);
    hgnfs::bm25::BM25Index bm25;
    for (auto& d : docs) {
        std::string bmtext = d.title + " " + d.text;
        // Always index at document level; sections handled via chunk prefixing
        bm25.add_document(d.id, bmtext);
    }
    auto tbm = std::chrono::high_resolution_clock::now();
    bm25.build();
    auto tbm2 = std::chrono::high_resolution_clock::now();
    fprintf(stderr, "%.0f ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(tbm2 - tbm).count() * 1.0);

    // Embedder
    auto& mgr = zetla::rag::RagManager::instance();
    mgr.remove_session("beir");
    mgr.shutdown();
    if (!mgr.init_embedder(model_dir)) { fprintf(stderr, "INIT FAIL\n"); return {}; }
    mgr.set_projection_enabled(false);
    mgr.create_session("beir");

    // Index with section support (parallel)
    fprintf(stderr, "Indexing... "); fflush(stderr);
    auto t0 = std::chrono::high_resolution_clock::now();
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < (int)docs.size(); ++i) {
        if (i % 500 == 0) { fprintf(stderr, "\r  doc %d/%zu", i, docs.size()); fflush(stderr); }
        if (use_sections) {
            auto secs = split_sections(docs[i].title + " " + docs[i].text);
            for (auto& [sec, txt] : secs)
                mgr.add_file("beir", docs[i].id, txt, sec, 500, 90);
        } else {
            mgr.add_file("beir", docs[i].id, docs[i].title + " " + docs[i].text, "", 500, 90);
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    auto idx_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() * 1.0;
    int nchunks = mgr.chunk_count("beir");
    fprintf(stderr, "\rIndexing: %.0f ms (%d chunks)\n", idx_ms, nchunks);

    // Search + evaluate
    fprintf(stderr, "Retrieval... "); fflush(stderr);
    auto ts0 = std::chrono::high_resolution_clock::now();
    double sum_ndcg = 0, sum_map = 0, sum_recall = 0;
    int nq = 0;

    // Build per-doc chunk index map for reranking
    std::unordered_map<std::string, std::vector<int>> doc_chunks;
    if (use_rerank) {
        for (int ci = 0; ci < mgr.chunk_count("beir"); ++ci) {
            // Get chunk metadata - need access to LorentzIndex internals
            // For now, build via search results
        }
    }

    #pragma omp parallel for schedule(dynamic) reduction(+:sum_ndcg,sum_map,sum_recall,nq)
    for (int qi = 0; qi < (int)queries.size(); ++qi) {
        auto& q = queries[qi];

        auto bm25_scores = bm25.search(q.query, 0);
        float bm_max = 0.0f;
        for (auto& [did, s] : bm25_scores) if (s > bm_max) bm_max = s;
        if (bm_max < 1e-6f) bm_max = 1.0f;
        std::unordered_map<std::string, float> bm_normed;
        for (auto& [did, s] : bm25_scores) bm_normed[did] = s / bm_max;

        std::vector<std::pair<std::string, float>> ranked;
        
        if (use_rerank) {
            // Fast reranking: BM25 -> candidates, Lorentz re-rank on chunks
            // Narrow to top-50 BM25 candidates for efficiency
            std::vector<std::pair<std::string, float>> bm_sorted(bm_normed.begin(), bm_normed.end());
            int n_cand = std::min(50, (int)bm_sorted.size());
            std::partial_sort(bm_sorted.begin(), bm_sorted.begin() + n_cand, bm_sorted.end(),
                [](auto& a, auto& b) { return a.second > b.second; });
            
            std::unordered_map<std::string, float> candidates;
            for (int i = 0; i < n_cand; ++i) candidates[bm_sorted[i].first] = bm_sorted[i].second;
            
            auto rj = mgr.search_rerank("beir", q.query, candidates, 100, alpha_bm25);
            // Parse reranked results
            size_t pos = 0;
            while ((pos = rj.find("\"path\":\"", pos)) != std::string::npos) {
                pos += 8; auto end = rj.find('"', pos);
                std::string did = rj.substr(pos, end - pos);
                auto sp = rj.find("\"score\":", end); sp += 8;
                auto se = rj.find_first_of(",}\n\r", sp);
                if (se == std::string::npos) se = rj.size();
                float s = std::stof(rj.substr(sp, se - sp));
                ranked.emplace_back(did, s);
                pos = end;
            }
        } else {
            // Standard: BM25 + hyperbolic combined
            auto json = mgr.search("beir", q.query, 200, "");
            std::unordered_map<std::string, float> hyp_scores;
            float hyp_max = -1e20f, hyp_min = 1e20f;
            size_t pos = 0;
            while ((pos = json.find("\"path\":\"", pos)) != std::string::npos) {
                pos += 8; auto end = json.find('"', pos);
                std::string did = json.substr(pos, end - pos);
                auto sp = json.find("\"score\":", end); sp += 8;
                auto se = json.find_first_of(",}\n\r", sp);
                if (se == std::string::npos) se = json.size();
                float s = std::stof(json.substr(sp, se - sp));
                float& cur = hyp_scores[did];
                if (s > cur) cur = s;
                if (s > hyp_max) hyp_max = s;
                if (s < hyp_min) hyp_min = s;
                pos = end;
            }
            for (auto& d : docs) {
                float h = hyp_scores.count(d.id) ? hyp_scores[d.id] : 0.0f;
                float h_norm = hyp_max > hyp_min ? (h - hyp_min) / (hyp_max - hyp_min + 1e-6f) : 0.0f;
                float b = bm_normed.count(d.id) ? bm_normed[d.id] : 0.0f;
                ranked.emplace_back(d.id, alpha_bm25 * b + (1.0f - alpha_bm25) * h_norm);
            }
            int k = std::min((int)ranked.size(), 100);
            std::partial_sort(ranked.begin(), ranked.begin() + k, ranked.end(),
                [](auto& a, auto& b) { return a.second > b.second; });
        }

        std::vector<std::string> doc_ranked;
        for (auto& r : ranked) doc_ranked.push_back(r.first);

        if (qrels.count(q.id)) {
            sum_ndcg += ndcg_at_k(doc_ranked, qrels, q.id, 10);
            sum_map += map_at_k(doc_ranked, qrels, q.id, 100);
            sum_recall += recall_at_k(doc_ranked, qrels, q.id, 100);
            nq++;
        }
    }
    auto ts1 = std::chrono::high_resolution_clock::now();
    auto query_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ts1 - ts0).count() * 1.0;
    fprintf(stderr, "%.0f ms (%.1f ms/q)\n", query_ms, query_ms / queries.size());

    RunResult r;
    r.ndcg10 = nq > 0 ? sum_ndcg / nq : 0.0;
    r.map100 = nq > 0 ? sum_map / nq : 0.0;
    r.recall100 = nq > 0 ? sum_recall / nq : 0.0;
    r.idx_ms = idx_ms;
    r.query_ms = query_ms;
    r.chunks = nchunks;
    return r;
}

int main(int argc, char* argv[]) {
    const char* model_dir = "Zetla\\data\\src\\main\\assets\\bge_model";

    bool all_datasets = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--all") == 0) all_datasets = true;
    }

    const char* dataset_list[] = {"scifact","nfcorpus","arguana","scidocs","fiqa"};
    int nds = all_datasets ? 5 : 1;

    if (!all_datasets) {
        const char* ds = argc > 1 ? argv[1] : "beir_bench/scifact";
        auto r = run_bench((std::string)ds, model_dir, 0.7f, false, false);
        printf("\n%-20s %8.4f %8.4f %8.4f %7.0f ms\n", "Word Embeds (a=0.7)",
               r.ndcg10, r.map100, r.recall100, r.idx_ms + r.query_ms);
    } else {
        // Run all configs × all datasets
        const char* configs[] = {"a=0.3","a=0.7","a=0.7+sec","a=0.7+sec+rerank","a=1.0"};
        float alphas[]    =        { 0.3f,   0.7f,   0.7f,      0.7f,             1.0f};
        bool   reranks[]  =        { false,  false,  false,     true,             false};
        bool   sections[] =        { false,  false,  true,      true,             false};

        for (int d = 0; d < nds; ++d) {
            char dir[256]; snprintf(dir, sizeof(dir), "beir_bench/%s", dataset_list[d]);
            printf("\n%s:\n", dataset_list[d]);
            printf("  %-18s %8s %8s %8s\n", "Config", "NDCG@10", "MAP@100", "R@100");
            for (int c = 0; c < 5; ++c) {
                auto r = run_bench(dir, model_dir, alphas[c], reranks[c], sections[c]);
                printf("  %-18s %8.4f %8.4f %8.4f\n", configs[c], r.ndcg10, r.map100, r.recall100);
            }
        }
    }
    return 0;
}
