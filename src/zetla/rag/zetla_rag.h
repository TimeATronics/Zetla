#pragma once

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// 
//  Hyperbolic RAG C API - consumer-friendly singleton interface
// 
//
//  1. zetla_rag_init(model_dir)         - load embeds, projection
//  2. zetla_rag_set_config(alpha, cs, co)- tune retrieval
//  3. zetla_rag_create_session(id)      - new corpus
//  4. zetla_rag_add_file(sid, fp, txt, sec)- add document
//  5. zetla_rag_search(sid, q, k)       - query, returns JSON
//  6. zetla_rag_shutdown()              - free memory
//
//  JSON result format:
//  [{"path":"doc.md","chunk_idx":5,"score":0.92,"text":"..."},...]
// 

// Lifecycle
int  zetla_rag_init(const char* model_dir);
void zetla_rag_shutdown(void);

// Configuration (call after init, before creating sessions)
void zetla_rag_set_config(float bm25_alpha,    // BM25 weight [0-1], default 0.7
                          int chunk_chars,     // chars per chunk, default 500
                          int overlap_chars);  // overlap chars, default 90

// Session management
int  zetla_rag_create_session(const char* session_id);
int  zetla_rag_save_session(const char* session_id, const char* dir_path);
int  zetla_rag_load_session(const char* session_id, const char* dir_path);
void zetla_rag_remove_session(const char* session_id);

// Document operations
int  zetla_rag_add_file(const char* session_id,
                        const char* file_path,
                        const char* text_content,
                        const char* section_hint);
int  zetla_rag_chunk_count(const char* session_id);
size_t zetla_rag_memory_bytes(const char* session_id);

// Search (returns JSON string - caller must free with zetla_rag_free)
char* zetla_rag_search(const char* session_id,
                       const char* query,
                       int top_k);

// Search with hyperbolic reranking (BM25 -> Lorentz re-rank)
char* zetla_rag_search_rerank(const char* session_id,
                              const char* query,
                              int top_k);

// Free search result string
void zetla_rag_free(char* ptr);

// List active session IDs (JSON array of strings, caller frees)
char* zetla_rag_list_sessions(void);

// Return codes
#define ZETLA_RAG_OK    0
#define ZETLA_RAG_ERR   -1
#define ZETLA_RAG_NOT_FOUND -2

#ifdef __cplusplus
}
#endif
