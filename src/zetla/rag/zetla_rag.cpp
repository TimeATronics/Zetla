#include "zetla_rag.h"
#include "rag_tool.hpp"
#include "bm25_index.hpp"
#include <string>
#include <cstring>
#include <unordered_map>
#include <android/log.h>
#define ZLOGI(...) __android_log_print(ANDROID_LOG_INFO, "ZetlaNative", __VA_ARGS__)
#include <unordered_set>

// Per-session BM25 index cache (search_rerank needs it)
static std::unordered_map<std::string, hgnfs::bm25::BM25Index> g_bm25;
static float g_alpha = 0.7f;
static int g_chunk_words = 300, g_overlap_words = 60;

int zetla_rag_init(const char* model_dir) {
    auto& mgr = zetla::rag::RagManager::instance();
    return mgr.init_embedder(model_dir) ? ZETLA_RAG_OK : ZETLA_RAG_ERR;
}

void zetla_rag_shutdown(void) {
    zetla::rag::RagManager::instance().shutdown();
    g_bm25.clear();
}

void zetla_rag_set_config(float bm25_alpha, int chunk_words, int overlap_words) {
    g_alpha = bm25_alpha;
    g_chunk_words = chunk_words;
    g_overlap_words = overlap_words;
}

int zetla_rag_create_session(const char* sid) {
    return zetla::rag::RagManager::instance().create_session(sid) ? ZETLA_RAG_OK : ZETLA_RAG_ERR;
}

int zetla_rag_save_session(const char* sid, const char* dir) {
    return zetla::rag::RagManager::instance().save_session(sid, dir) ? ZETLA_RAG_OK : ZETLA_RAG_ERR;
}

int zetla_rag_load_session(const char* sid, const char* dir) {
    return zetla::rag::RagManager::instance().load_session(sid, dir) ? ZETLA_RAG_OK : ZETLA_RAG_ERR;
}

void zetla_rag_remove_session(const char* sid) {
    zetla::rag::RagManager::instance().remove_session(sid);
    g_bm25.erase(sid);
}

int zetla_rag_add_file(const char* sid, const char* fp, const char* text, const char* section) {
    auto& mgr = zetla::rag::RagManager::instance();
    
    // Maintain BM25 index
    auto bmit = g_bm25.find(sid);
    if (bmit == g_bm25.end()) {
        hgnfs::bm25::BM25Index idx;
        idx.add_document(fp, text);
        bmit = g_bm25.emplace(sid, std::move(idx)).first;
    } else {
        bmit->second.add_document(fp, text);
    }
    // Defer build - called lazily on first search_rerank
    
    return mgr.add_file(sid, fp, text, section ? section : "", g_chunk_words, g_overlap_words);
}

int zetla_rag_chunk_count(const char* sid) {
    return zetla::rag::RagManager::instance().chunk_count(sid);
}

size_t zetla_rag_memory_bytes(const char* sid) {
    return zetla::rag::RagManager::instance().memory_bytes(sid);
}

static char* strdup_c(const std::string& s) {
    char* p = (char*)malloc(s.size() + 1);
    if (p) { memcpy(p, s.c_str(), s.size() + 1); }
    return p;
}

char* zetla_rag_search(const char* sid, const char* query, int top_k) {
    auto& mgr = zetla::rag::RagManager::instance();
    int chunks = mgr.chunk_count(sid);
    ZLOGI("[rag_search] sid=%s query=%.40s chunks=%d", sid, query, chunks);
    std::string result = mgr.search(sid, query, top_k, "");
    return strdup_c(result);
}

char* zetla_rag_search_rerank(const char* sid, const char* query, int top_k) {
    auto& mgr = zetla::rag::RagManager::instance();
    auto bmit = g_bm25.find(sid);
    if (bmit == g_bm25.end()) {
        std::string result = mgr.search(sid, query, top_k, "");
        return strdup_c(result);
    }
    // Build if needed (first time)
    static std::unordered_set<std::string> built;
    if (!built.count(sid)) { bmit->second.build(); built.insert(sid); }
    
    auto scores = bmit->second.search(query, 0);
    float bm_max = 0.0f;
    for (auto& [did, s] : scores) if (s > bm_max) bm_max = s;
    if (bm_max < 1e-6f) bm_max = 1.0f;
    std::unordered_map<std::string, float> normed;
    for (auto& [did, s] : scores) normed[did] = s / bm_max;
    
    std::string result = mgr.search_rerank(sid, query, normed, top_k, g_alpha);
    return strdup_c(result);
}

void zetla_rag_free(char* ptr) { free(ptr); }

char* zetla_rag_list_sessions(void) {
    std::string json = "[";
    for (auto& [sid, _] : g_bm25) {
        if (json.size() > 1) json += ",";
        json += "\"" + sid + "\"";
    }
    json += "]";
    return strdup_c(json);
}
