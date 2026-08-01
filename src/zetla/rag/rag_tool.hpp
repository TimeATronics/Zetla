#pragma once
#include "bm25_index.hpp"
#include "core/types.hpp"              // hgnfs LorentzPoint, ChunkMeta
#include "../core/types.hpp"            // zetla IToolExecutor, ToolCallRequest, ToolCallResult
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace zetla::rag {

// Per-session RAG manager - created when files are attached
class RagManager {
public:
    static RagManager& instance();

    bool init_embedder(const std::string& model_path);
    void set_projection_enabled(bool enabled);
    bool projection_enabled() const;

    // Reranking toggle (BM25 + Lorentz two-pass vs pure Lorentz)
    void set_rerank_enabled(bool enabled);
    bool rerank_enabled() const;

    // Retrieval tuning (defaults: alpha=0.7, chunk=300w, overlap=60w)
    void set_config(float bm25_alpha, int chunk_chars, int overlap_chars);
    float get_alpha() const;
    int get_chunk_chars() const;
    int get_overlap_chars() const;

    bool create_session(const std::string& session_id);

    int add_file(const std::string& session_id,
                 const std::string& file_path,
                 const std::string& text_content,
                 const std::string& section = "",
                 int chunk_chars = 0,
                 int overlap_chars = 0);

    std::string search(const std::string& session_id,
                       const std::string& query,
                       int top_k = 5,
                       const std::string& scope_file = "");

    // Hyperbolic reranking: re-score only the top BM25 candidates.
    // Returns JSON with re-ranked document scores (+ best chunk text).
    std::string search_rerank(const std::string& session_id,
                              const std::string& query,
                              const std::unordered_map<std::string, float>& bm25_scores,
                              int top_k = 10,
                              float alpha_bm25 = 0.7f);

    // Convenience overload: BM25 scores come from the session's own index.
    // Falls back to plain search when no BM25 index has been built.
    std::string search_rerank(const std::string& session_id,
                              const std::string& query,
                              int top_k = 10);

    int chunk_count(const std::string& session_id) const;
    size_t memory_bytes(const std::string& session_id) const;
    std::vector<std::string> list_sessions() const;
    void remove_session(const std::string& session_id);
    void shutdown();

    // Persistence
    bool save_session(const std::string& session_id, const std::string& path) const;
    bool load_session(const std::string& session_id, const std::string& path);

    // List unique file paths in a session's corpus
    std::vector<std::string> list_files(const std::string& session_id) const;

    // Graph enhancement: build k-NN graph + smooth embeddings for hierarchical refinement
    void enhance_with_graph(const std::string& session_id, int k = 5);

    using DebugFn = void(*)(const char*);
    void set_debug_callback(DebugFn fn);

private:
    RagManager() = default;
    struct SessionRagIndex;
    std::unordered_map<std::string, std::unique_ptr<SessionRagIndex>> sessions_;
    DebugFn debug_fn_ = nullptr;
    bool embedder_ready_ = false;
    float alpha_bm25_ = 0.7f;
    int chunk_chars_ = 300;
    int overlap_chars_ = 60;
    bool rerank_enabled_ = true;
    bool proj_disabled_ = false;
    void log(const std::string& msg);

public:
    struct EmbedderState;
    std::unique_ptr<EmbedderState> embedder_;
};

//  Tool 

class RagTool : public core::IToolExecutor {
public:
    explicit RagTool(const std::string& session_id);

    std::string name() const override { return "search_files"; }
    std::string description() const override;
    std::string parameters_schema() const override;
    core::ToolCallResult execute(const core::ToolCallRequest& call) override;

private:
    std::string session_id_;
};

class CorpusFilesTool : public core::IToolExecutor {
public:
    explicit CorpusFilesTool(const std::string& session_id);

    std::string name() const override { return "list_corpus_files"; }
    std::string description() const override;
    std::string parameters_schema() const override;
    core::ToolCallResult execute(const core::ToolCallRequest& call) override;

private:
    std::string session_id_;
};

}  // namespace zetla::rag
