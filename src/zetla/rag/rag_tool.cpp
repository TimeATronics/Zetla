#include "rag_tool.hpp"
#include "bert_tokenizer.hpp"
#include "hyp_embedder.hpp"
#include "graph_enhance.hpp"
#include "core/lorentz.hpp"
#include "index/lorentz_index.hpp"
#include "chunker/chunker.hpp"
#include "api/hgnfs_api.h"
#include <nlohmann/json.hpp>
#include "../core/log.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <memory>
#include <mutex>
#include <fstream>
#include <zlib.h>

using json = nlohmann::json;

namespace zetla::rag {

struct RagManager::SessionRagIndex {
    hgnfs::index::LorentzIndex idx{385};
    hgnfs::bm25::BM25Index bm25;
    bool bm25_built = false;
    std::vector<std::string> chunk_texts;
    std::vector<std::string> file_paths;
    std::mutex mtx;
};

struct RagManager::EmbedderState {
    hgnfs::hyp::HyperEmbedder embedder;
    BERTTokenizer tokenizer;
    bool loaded = false;
};

RagManager& RagManager::instance() { static RagManager mgr; return mgr; }
void RagManager::log(const std::string& msg) { ZLOGI("%s", msg.c_str()); if (debug_fn_) debug_fn_(msg.c_str()); }

bool RagManager::init_embedder(const std::string& model_dir) {
    if (embedder_ready_) return true;
    auto st = std::make_unique<EmbedderState>();
    if (!st->tokenizer.load_vocab(model_dir + "/vocab.txt")) { log("[RAG] FATAL: vocab"); return false; }
    if (!st->embedder.init(model_dir)) { log("[RAG] WARNING: no word embeds"); }
    st->loaded = true;
    embedder_ = std::move(st);
    embedder_ready_ = true;
    log("[RAG] Embedder ready (hyperbolic word embeds)");
    return true;
}

void RagManager::set_projection_enabled(bool enabled) {
    proj_disabled_ = !enabled;
    if (embedder_) {
        embedder_->embedder.set_projection_enabled(enabled);
    }
}

bool RagManager::projection_enabled() const { return !proj_disabled_; }

void RagManager::set_rerank_enabled(bool enabled) { rerank_enabled_ = enabled; }
bool RagManager::rerank_enabled() const { return rerank_enabled_; }

void RagManager::set_config(float bm25_alpha, int chunk_chars, int overlap_chars) {
    alpha_bm25_ = bm25_alpha;
    if (chunk_chars > 0) chunk_chars_ = chunk_chars;
    if (overlap_chars > 0) overlap_chars_ = overlap_chars;
}

float RagManager::get_alpha() const { return alpha_bm25_; }
int RagManager::get_chunk_chars() const { return chunk_chars_; }
int RagManager::get_overlap_chars() const { return overlap_chars_; }

static std::vector<float> embed_text(const std::string& text, RagManager::EmbedderState& st) {
    auto tokens = st.tokenizer.encode(text, 256);
    return st.embedder.embed_tokens(tokens.input_ids);
}

bool RagManager::create_session(const std::string& id) {
    if (sessions_.count(id)) return true;
    sessions_[id] = std::make_unique<SessionRagIndex>();
    return true;
}

int RagManager::add_file(const std::string& sid, const std::string& fp,
                         const std::string& text, const std::string& section,
                         int chunk_chars, int overlap_chars) {
    auto it = sessions_.find(sid);
    if (it == sessions_.end() || !embedder_ || !embedder_->loaded) return -1;
    auto& sess = *it->second;

    if (chunk_chars <= 0) chunk_chars = chunk_chars_;
    if (overlap_chars <= 0) overlap_chars = overlap_chars_;

    auto chunks = hgnfs::chunker::chunk_text(text, chunk_chars, overlap_chars, section);
    if (chunks.empty()) return 0;

    ZLOGI("[add_file] chunking done: %zu chunks for '%s'", chunks.size(), fp.c_str());

    std::lock_guard<std::mutex> lock(sess.mtx);

    // Maintain per-session BM25 index for hybrid reranking
    sess.bm25.add_document(fp, text);
    // Defer build - called once before first search via search_rerank

    ZLOGI("[add_file] embedding %zu chunks...", chunks.size());
    std::vector<std::vector<float>> embeddings;
    embeddings.reserve(chunks.size());
    for (auto& c : chunks)
        embeddings.push_back(embed_text(c.text, *embedder_));
    ZLOGI("[add_file] embedding done");

    int ci = (int)sess.chunk_texts.size();
    for (size_t i = 0; i < chunks.size(); ++i) {
        sess.chunk_texts.push_back(chunks[i].text);
        sess.file_paths.push_back(fp);
        sess.idx.add(embeddings[i].data(), (int)embeddings[i].size(), {fp, ci++});
    }
    ZLOGI("[rag] session=%s chunks=%d total_chunks=%d size=%zu KB", sid.c_str(), (int)chunks.size(), sess.idx.n_chunks(), sess.idx.memory_bytes() / 1024);
    return (int)chunks.size();
}

static std::string snippet(const std::string& text, size_t max_chars = 400) {
    std::string s = text;
    if (s.size() > max_chars) s = s.substr(0, max_chars) + "...";
    return s;
}

std::string RagManager::search(const std::string& sid, const std::string& q, int k, const std::string& sc) {
    auto it = sessions_.find(sid);
    if (it == sessions_.end() || !embedder_ || !embedder_->loaded) return "[]";
    auto& sess = *it->second;
    if (sess.idx.n_chunks() == 0) return "[]";
    auto qv = embed_text(q, *embedder_);
    auto results = sess.idx.search(qv.data(), (int)qv.size(), k, sc.empty() ? nullptr : sc.c_str());
    json j = json::array();
    for (auto& r : results) {
        json item;
        item["chunk_idx"] = r.chunk_idx; item["path"] = r.path;
        item["score"] = r.score; item["distance"] = r.distance;
        if (r.chunk_idx >= 0 && r.chunk_idx < (int)sess.chunk_texts.size())
            item["text"] = snippet(sess.chunk_texts[r.chunk_idx]);
        j.push_back(item);
    }
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string RagManager::search_rerank(const std::string& sid, const std::string& q,
                                       const std::unordered_map<std::string, float>& bm25_scores,
                                       int top_k, float alpha_bm25) {
    auto it = sessions_.find(sid);
    if (it == sessions_.end() || !embedder_ || !embedder_->loaded) return "[]";
    auto& sess = *it->second;
    int N = sess.idx.n_chunks();
    if (N == 0) return "[]";

    auto qv = embed_text(q, *embedder_);
    int dim = (int)qv.size();

    // Build per-doc chunk index map and compute BM25 norms
    std::unordered_map<std::string, std::vector<int>> doc_chunks;
    float bm_max = 0.0f;
    for (auto& [did, s] : bm25_scores) if (s > bm_max) bm_max = s;
    if (bm_max < 1e-6f) bm_max = 1.0f;

    for (int ci = 0; ci < N; ++ci) {
        doc_chunks[sess.file_paths[ci]].push_back(ci);
    }

    // For each BM25 candidate, compute best Lorentz score from its chunks
    std::vector<std::pair<std::string, float>> ranked;
    std::unordered_map<std::string, int> best_chunk;
    for (auto& [did, bm_score] : bm25_scores) {
        auto dc = doc_chunks.find(did);
        if (dc == doc_chunks.end() || dc->second.empty()) continue;

        auto lorentz_scores = sess.idx.rerank(qv.data(), dim, dc->second);
        float best_l = -1e20f;
        int best_ci = dc->second[0];
        for (size_t i = 0; i < lorentz_scores.size(); ++i) {
            float s = lorentz_scores[i];
            if (s > best_l) { best_l = s; best_ci = dc->second[i]; }
        }

        // Normalize Lorentz score (inner product ranges from -∞ to 0 for same-manifold)
        // Map to [0,1]: higher inner = more similar
        float l_norm = 1.0f / (1.0f + std::exp(-best_l - 2.0f));  // sigmoid

        float final_score = alpha_bm25 * (bm_score / bm_max) + (1.0f - alpha_bm25) * l_norm;
        ranked.emplace_back(did, final_score);
        best_chunk[did] = best_ci;
    }

    // Sort and return top-k
    int k = std::min(top_k, (int)ranked.size());
    std::partial_sort(ranked.begin(), ranked.begin() + k, ranked.end(),
        [](auto& a, auto& b) { return a.second > b.second; });

    json j = json::array();
    for (int i = 0; i < k; ++i) {
        json item;
        item["path"] = ranked[i].first;
        item["score"] = ranked[i].second;
        auto bci = best_chunk.find(ranked[i].first);
        if (bci != best_chunk.end()) {
            item["chunk_idx"] = bci->second;
            if (bci->second >= 0 && bci->second < (int)sess.chunk_texts.size())
                item["text"] = snippet(sess.chunk_texts[bci->second]);
        }
        j.push_back(item);
    }
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string RagManager::search_rerank(const std::string& sid, const std::string& q, int top_k) {
    auto it = sessions_.find(sid);
    if (it == sessions_.end()) return "[]";
    auto& sess = *it->second;

    std::unordered_map<std::string, float> scores;
    {
        std::lock_guard<std::mutex> lock(sess.mtx);
        if (!sess.bm25_built) {
            sess.bm25_built = true;
            sess.bm25.build();
        }
        scores = sess.bm25.search(q, 0);
    }
    if (scores.empty()) return search(sid, q, top_k, "");

    float bm_max = 0.0f;
    for (auto& [did, s] : scores) if (s > bm_max) bm_max = s;
    if (bm_max < 1e-6f) bm_max = 1.0f;
    std::unordered_map<std::string, float> normed;
    for (auto& [did, s] : scores) normed[did] = s / bm_max;

    return search_rerank(sid, q, normed, top_k, alpha_bm25_);
}

int RagManager::chunk_count(const std::string& sid) const {
    auto it = sessions_.find(sid);
    return (it != sessions_.end()) ? it->second->idx.n_chunks() : 0;
}
size_t RagManager::memory_bytes(const std::string& sid) const {
    auto it = sessions_.find(sid);
    return (it != sessions_.end()) ? it->second->idx.memory_bytes() : 0;
}
void RagManager::remove_session(const std::string& sid) { sessions_.erase(sid); }
void RagManager::shutdown() { sessions_.clear(); embedder_.reset(); embedder_ready_ = false; }

std::vector<std::string> RagManager::list_sessions() const {
    std::vector<std::string> ids;
    ids.reserve(sessions_.size());
    for (auto& [sid, _] : sessions_) ids.push_back(sid);
    return ids;
}

bool RagManager::save_session(const std::string& sid, const std::string& dir) const {
    auto it = sessions_.find(sid);
    if (it == sessions_.end()) return false;
    auto& sess = *it->second;
    if (!sess.idx.save(dir + "/idx.bin")) return false;

    // Compressed texts
    {
        std::string all_texts;
        for (auto& t : sess.chunk_texts) {
            int32_t l = (int32_t)t.size();
            all_texts.append((const char*)&l, 4);
            all_texts.append(t.data(), l);
        }
        uLongf dest_len = compressBound((uLong)all_texts.size());
        std::vector<uint8_t> compressed(dest_len);
        if (compress2(compressed.data(), &dest_len, (const uint8_t*)all_texts.data(), (uLong)all_texts.size(), Z_BEST_COMPRESSION) != Z_OK)
            return false;
        compressed.resize(dest_len);
        std::ofstream ft(dir + "/texts.z", std::ios::binary);
        if (!ft) return false;
        ft.write((const char*)compressed.data(), compressed.size());
    }

    // Paths (tiny, no compression needed)
    std::ofstream fp(dir + "/paths.txt", std::ios::binary);
    if (!fp) return false;
    for (auto& p : sess.file_paths) { int32_t l = (int32_t)p.size(); fp.write((const char*)&l, 4); fp.write(p.data(), l); }
    return fp.good();
}

bool RagManager::load_session(const std::string& sid, const std::string& dir) {
    auto sess = std::make_unique<SessionRagIndex>();
    if (!sess->idx.load(dir + "/idx.bin")) return false;

    // Decompress texts
    {
        std::ifstream ft(dir + "/texts.z", std::ios::binary | std::ios::ate);
        if (!ft) return false;
        size_t fsz = ft.tellg(); ft.seekg(0);
        std::vector<uint8_t> compressed(fsz);
        ft.read((char*)compressed.data(), fsz);
        
        uLongf dest_len = fsz * 4;
        std::vector<uint8_t> decompressed;
        int ret;
        do {
            decompressed.resize(dest_len);
            ret = uncompress(decompressed.data(), &dest_len, compressed.data(), (uLong)fsz);
            if (ret == Z_BUF_ERROR) dest_len *= 2;
        } while (ret == Z_BUF_ERROR);
        if (ret != Z_OK) return false;
        decompressed.resize(dest_len);

        const char* p = (const char*)decompressed.data();
        const char* end = p + dest_len;
        while (p + 4 <= end) {
            int32_t l; memcpy(&l, p, 4); p += 4;
            if (p + l > end) break;
            sess->chunk_texts.push_back(std::string(p, l));
            p += l;
        }
    }

    // Load paths
    {
        std::ifstream f(dir + "/paths.txt", std::ios::binary);
        if (!f) return false;
        while (f.peek() != EOF) {
            int32_t l; f.read((char*)&l, 4); std::string s(l, '\0'); f.read(&s[0], l);
            sess->file_paths.push_back(s);
        }
    }
    sessions_[sid] = std::move(sess);
    return true;
}
void RagManager::set_debug_callback(DebugFn fn) { debug_fn_ = fn; }
void RagManager::enhance_with_graph(const std::string& sid, int k) {
    auto it = sessions_.find(sid);
    if (it == sessions_.end()) return;
    auto& sess = *it->second;
    int N = sess.idx.n_chunks(), dim = sess.idx.dim();
    if (N < 3) return;
    auto raw = sess.idx.raw_data();
    auto adj = hgnfs::gnn::build_knn_graph(raw.data(), N, dim, k);
    std::vector<float> X_smooth;
    hgnfs::gnn::smooth_embeddings(raw, N, dim, adj, X_smooth);
    auto meta = sess.idx.meta_snapshot();
    sess.idx = hgnfs::index::LorentzIndex(dim);
    for (int i = 0; i < N; ++i) sess.idx.add(&X_smooth[i * dim], dim, meta[i]);
}

RagTool::RagTool(const std::string& sid) : session_id_(sid) {}
std::string RagTool::description() const { return "Hyperbolic semantic retrieval."; }
std::string RagTool::parameters_schema() const { return R"({"type":"object","properties":{"query":{"type":"string"}},"required":["query"]})"; }
core::ToolCallResult RagTool::execute(const core::ToolCallRequest& c) {
    core::ToolCallResult r; r.tool_call_id = c.id;
    try {
        auto q = nlohmann::json::parse(c.arguments_json).value("query","");
        auto& mgr = RagManager::instance();
        ZLOGI("[RagTool] execute sid=%s query=%.40s chunks=%d rerank=%d",
              session_id_.c_str(), ((std::string)q).c_str(), mgr.chunk_count(session_id_),
              (int)mgr.rerank_enabled());
        if (mgr.rerank_enabled()) {
            r.content = mgr.search_rerank(session_id_, q, 5);
        } else {
            r.content = mgr.search(session_id_, q, 5, "");
        }
    }
    catch (const std::exception& e) { r.is_error = true; r.content = e.what(); }
    return r;
}

std::vector<std::string> RagManager::list_files(const std::string& sid) const {
    auto it = sessions_.find(sid);
    if (it == sessions_.end()) return {};
    auto& sess = *it->second;
    std::unordered_set<std::string> seen;
    std::vector<std::string> result;
    for (auto& fp : sess.file_paths) {
        if (seen.insert(fp).second) result.push_back(fp);
    }
    return result;
}

CorpusFilesTool::CorpusFilesTool(const std::string& sid) : session_id_(sid) {}
std::string CorpusFilesTool::description() const { return "List all document file names in the corpus."; }
std::string CorpusFilesTool::parameters_schema() const { return R"({"type":"object","properties":{}})"; }
core::ToolCallResult CorpusFilesTool::execute(const core::ToolCallRequest& c) {
    core::ToolCallResult r; r.tool_call_id = c.id;
    try {
        auto files = RagManager::instance().list_files(session_id_);
        json j = json::array();
        for (auto& fp : files) {
            j.push_back(fp.substr(fp.find_last_of("/\\") + 1));
        }
        r.content = j.dump();
    }
    catch (const std::exception& e) { r.is_error = true; r.content = e.what(); }
    return r;
}

}  // namespace zetla::rag
