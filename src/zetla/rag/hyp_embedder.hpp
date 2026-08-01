#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace hgnfs::hyp {

class EmbeddingTable {
public:
    bool load(const std::string& path);
    const float* get(int token_id) const;
    int vocab_size() const { return vocab_size_; }
    int embed_dim() const { return embed_dim_; }
private:
    std::vector<float> data_;
    int vocab_size_ = 0;
    int embed_dim_ = 0;
};

// HyTE-H: Learned Lorentz projection matrix (trained per HypRAG paper)
class LorentzProjection {
public:
    bool load(const std::string& model_dir);
    void project(const float* input, float* output) const;
    void project_batch(const float* inputs, float* outputs, int batch_size) const;
    int in_dim() const { return embed_dim_; }
    int hid_dim() const { return hidden_dim_; }
    int out_dim() const { return embed_dim_; }
    bool loaded() const { return loaded_; }
private:
    std::vector<float> proj_w_;  // [hidden * embed]
    std::vector<float> proj_b_;  // [hidden]
    std::vector<float> out_w_;   // [embed * hidden]
    std::vector<float> out_b_;   // [embed]
    int embed_dim_ = 384;
    int hidden_dim_ = 256;
    bool loaded_ = false;
};

class HyperEmbedder {
public:
    bool init(const std::string& model_dir);
    std::vector<float> embed_tokens(const std::vector<int64_t>& token_ids) const;

    int embed_dim() const { return embed_table_ ? embed_table_->embed_dim() : 64; }
    int hyp_dim() const { return embed_dim() + 1; }
    const EmbeddingTable* table() const { return embed_table_.get(); }
    bool has_projection() const { return projection_ && projection_->loaded() && !proj_disabled_; }
    void set_projection_enabled(bool e) { proj_disabled_ = !e; }

private:
    std::unique_ptr<EmbeddingTable> embed_table_;
    std::unique_ptr<LorentzProjection> projection_;
    float curvature_ = 1.0f;
    bool proj_disabled_ = true;  // disabled by default (faster on mobile)

    void apply_projection(const float* embed, float* proj_out) const;
    std::vector<float> add_time(const std::vector<float>& euclidean) const;
    std::vector<float> oem_pool(const std::vector<std::vector<float>>& points, float p = 1.0f) const;
    std::vector<float> einstein_midpoint(const std::vector<std::vector<float>>& points) const;
};

}  // namespace hgnfs::hyp
