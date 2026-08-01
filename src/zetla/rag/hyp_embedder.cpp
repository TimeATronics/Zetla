#include "hyp_embedder.hpp"
#include "core/lorentz.hpp"
#include "core/types.hpp"
#include <cmath>
#include <fstream>
#include <algorithm>
#include <cstring>

namespace hgnfs::hyp {

//  EmbeddingTable 
bool EmbeddingTable::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    int32_t vs, ed;
    f.read(reinterpret_cast<char*>(&vs), sizeof(vs));
    f.read(reinterpret_cast<char*>(&ed), sizeof(ed));
    if (vs <= 0 || ed <= 0) return false;
    vocab_size_ = vs; embed_dim_ = ed;
    data_.resize(vs * ed);
    f.read(reinterpret_cast<char*>(data_.data()), data_.size() * sizeof(float));
    return f.good() || f.eof();
}
const float* EmbeddingTable::get(int token_id) const {
    if (token_id < 0 || token_id >= vocab_size_) return data_.data();
    return &data_[token_id * embed_dim_];
}

//  LorentzProjection (HyTE-H trained weights) 
bool LorentzProjection::load(const std::string& model_dir) {
    // Load proj_weight.bin: [hidden × embed]
    std::string pw = model_dir + "/proj_weight.bin";
    std::ifstream f1(pw, std::ios::binary);
    if (!f1) return false;
    int32_t hd, ed;
    f1.read(reinterpret_cast<char*>(&hd), sizeof(hd));
    f1.read(reinterpret_cast<char*>(&ed), sizeof(ed));
    hidden_dim_ = hd; embed_dim_ = ed;
    proj_w_.resize(hd * ed);
    f1.read(reinterpret_cast<char*>(proj_w_.data()), proj_w_.size() * sizeof(float));

    // Load proj_out.bin: [embed × hidden]
    std::string po = model_dir + "/proj_out.bin";
    std::ifstream f2(po, std::ios::binary);
    if (!f2) return false;
    int32_t od, hd2;
    f2.read(reinterpret_cast<char*>(&od), sizeof(od));
    f2.read(reinterpret_cast<char*>(&hd2), sizeof(hd2));
    out_w_.resize(od * hd2);
    f2.read(reinterpret_cast<char*>(out_w_.data()), out_w_.size() * sizeof(float));

    loaded_ = true;
    return true;
}

void LorentzProjection::project(const float* input, float* output) const {
    project_batch(input, output, 1);
}

void LorentzProjection::project_batch(const float* inputs, float* outputs, int batch_size) const {
    const int hd = hidden_dim_, ed = embed_dim_;
    std::vector<float> hidden(hd * batch_size);
    
    // Hidden[b][h] = Relu(Σ_i W1[h][i] * input[b][i])
    for (int h = 0; h < hd; ++h) {
        const float* w_row = &proj_w_[h * ed];
        const float* inp = inputs;
        float* hid = hidden.data() + h;
        for (int b = 0; b < batch_size; ++b) {
            float sum = 0.0f;
            for (int i = 0; i < ed; ++i) sum += w_row[i] * inp[i];
            *hid = std::max(0.0f, sum);
            inp += ed;
            hid += hd;
        }
    }
    
    // Outputs[b][o] = Σ_h W2[o][h] * hidden[b][h]
    for (int o = 0; o < ed; ++o) {
        const float* ow_row = &out_w_[o * hd];
        const float* hid = hidden.data();
        float* out = outputs + o;
        for (int b = 0; b < batch_size; ++b) {
            float sum = 0.0f;
            for (int h = 0; h < hd; ++h) sum += ow_row[h] * hid[h];
            *out = sum;
            hid += hd;
            out += ed;
        }
    }
}

//  HyperEmbedder 
bool HyperEmbedder::init(const std::string& model_dir) {
    auto table = std::make_unique<EmbeddingTable>();
    if (!table->load(model_dir + "/word_embeds.bin")) return false;
    embed_table_ = std::move(table);

    auto proj = std::make_unique<LorentzProjection>();
    if (proj->load(model_dir)) {
        if (proj->in_dim() == embed_dim()) {
            projection_ = std::move(proj);
        }
    }
    return true;
}

void HyperEmbedder::apply_projection(const float* embed, float* proj_out) const {
    if (has_projection() && projection_->in_dim() == embed_dim()) {
        projection_->project_batch(embed, proj_out, 1);
    } else {
        std::memcpy(proj_out, embed, embed_dim() * sizeof(float));
    }
}

std::vector<float> HyperEmbedder::add_time(const std::vector<float>& euclidean) const {
    int d = (int)euclidean.size();
    float n2 = 0.0f;
    for (float v : euclidean) n2 += v * v;
    float time = std::sqrt(n2 + curvature_);
    if (time < 1e-6f) time = 1e-6f;
    std::vector<float> hyp(d + 1);
    hyp[0] = time;
    for (int i = 0; i < d; ++i) hyp[i + 1] = euclidean[i];
    return hyp;
}

// OEM pooling: weight ∝ x₀^(p+1), reproject to hyperboloid
std::vector<float> HyperEmbedder::oem_pool(
    const std::vector<std::vector<float>>& points, float p) const {
    if (points.empty()) {
        std::vector<float> origin(hyp_dim(), 0.0f);
        origin[0] = std::sqrt(curvature_);
        return origin;
    }
    int d = embed_dim();
    int n = (int)points.size();

    std::vector<double> wsum(d + 1, 0.0);
    double wtotal = 0.0;
    float exp = p + 1.0f;
    for (int i = 0; i < n; ++i) {
        float t = points[i][0];
        float w = (exp == 2.0f) ? (t * t) : std::pow(t, exp);
        wsum[0] += w * t;
        for (int j = 0; j < d; ++j) wsum[j + 1] += w * points[i][j + 1];
        wtotal += w;
    }
    if (wtotal < 1e-10f) {
        std::vector<float> origin(hyp_dim(), 0.0f);
        origin[0] = std::sqrt(curvature_);
        return origin;
    }

    // Ambient average
    std::vector<float> ambient(d + 1);
    for (int j = 0; j < d + 1; ++j) ambient[j] = (float)(wsum[j] / wtotal);

    // Reproject: scale to satisfy -t² + Σx² = -c
    float inner = -ambient[0] * ambient[0];
    for (int j = 0; j < d; ++j) inner += ambient[j + 1] * ambient[j + 1];
    if (inner >= 0.0f) return einstein_midpoint(points);  // fallback

    float scale = std::sqrt(curvature_ / -inner);
    std::vector<float> result(d + 1);
    for (int j = 0; j < d + 1; ++j) result[j] = ambient[j] * scale;
    return result;
}

static LorentzPoint to_lorentz(const std::vector<float>& hyp) {
    LorentzPoint p; p.t = hyp[0];
    p.x.assign(hyp.begin() + 1, hyp.end());
    return p;
}

std::vector<float> HyperEmbedder::einstein_midpoint(
    const std::vector<std::vector<float>>& points) const {
    if (points.empty()) {
        std::vector<float> origin(embed_dim() + 1, 0.0f);
        origin[0] = std::sqrt(curvature_);
        return origin;
    }
    std::vector<LorentzPoint> lps;
    for (auto& p : points) lps.push_back(to_lorentz(p));
    auto mid = lorentz::einstein_midpoint(lps.data(), (int)lps.size(), nullptr);
    std::vector<float> result(embed_dim() + 1);
    result[0] = mid.t;
    for (int i = 0; i < embed_dim(); ++i) result[i + 1] = mid.x[i];
    return result;
}

std::vector<float> HyperEmbedder::embed_tokens(const std::vector<int64_t>& token_ids) const {
    if (!embed_table_ || token_ids.empty()) {
        std::vector<float> origin(hyp_dim(), 0.0f);
        origin[0] = std::sqrt(curvature_);
        return origin;
    }
    int dim = embed_table_->embed_dim();
    std::vector<std::vector<float>> hyp_points;
    hyp_points.reserve(token_ids.size());

    if (has_projection() && projection_->in_dim() == embed_dim()) {
        int n_valid = 0;
        std::vector<int> valid_ids;
        valid_ids.reserve(token_ids.size());
        for (auto id : token_ids) {
            if (id > 0 && id < embed_table_->vocab_size()) valid_ids.push_back((int)id);
        }
        n_valid = (int)valid_ids.size();
        if (n_valid == 0) {
            std::vector<float> origin(hyp_dim(), 0.0f);
            origin[0] = std::sqrt(curvature_);
            return origin;
        }
        
        std::vector<float> inputs(n_valid * dim);
        for (int i = 0; i < n_valid; ++i) {
            const float* vec = embed_table_->get(valid_ids[i]);
            std::memcpy(&inputs[i * dim], vec, dim * sizeof(float));
        }
        
        std::vector<float> outputs(n_valid * dim);
        projection_->project_batch(inputs.data(), outputs.data(), n_valid);
        
        std::vector<float> hp_buf(dim + 1);
        for (int i = 0; i < n_valid; ++i) {
            const float* proj = &outputs[i * dim];
            float n2 = 0.0f;
            for (int j = 0; j < dim; ++j) n2 += proj[j] * proj[j];
            float t = std::sqrt(n2 + curvature_);
            if (t < 1e-6f) t = 1e-6f;
            hp_buf[0] = t;
            for (int j = 0; j < dim; ++j) hp_buf[j + 1] = proj[j];
            hyp_points.push_back(hp_buf);
        }
        
        return oem_pool(hyp_points, 1.0f);
    }
    
    // No projection: Einstein midpoint (original word embeddings -> hyperbolic)
    std::vector<float> proj_buf(dim);
    std::vector<float> hp_buf(dim + 1);
    for (auto id : token_ids) {
        if (id <= 0 || id >= embed_table_->vocab_size()) continue;
        const float* vec = embed_table_->get((int)id);
        float n2 = 0.0f;
        for (int j = 0; j < dim; ++j) n2 += vec[j] * vec[j];
        float t = std::sqrt(n2 + curvature_);
        if (t < 1e-6f) t = 1e-6f;
        hp_buf[0] = t;
        for (int j = 0; j < dim; ++j) hp_buf[j + 1] = vec[j];
        hyp_points.push_back(hp_buf);
    }
    
    if (hyp_points.empty()) {
        std::vector<float> origin(hyp_dim(), 0.0f);
        origin[0] = std::sqrt(curvature_);
        return origin;
    }
    return einstein_midpoint(hyp_points);
}

}  // namespace hgnfs::hyp
