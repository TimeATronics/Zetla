#include "bm25_index.hpp"
#include <cmath>
#include <cctype>
#include <sstream>
#include <algorithm>

namespace hgnfs::bm25 {

static std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : text) {
        if (std::isalnum((unsigned char)c)) {
            cur += (char)std::tolower((unsigned char)c);
        } else {
            if (!cur.empty() && cur.size() >= 2) { tokens.push_back(cur); cur.clear(); }
        }
    }
    if (!cur.empty() && cur.size() >= 2) tokens.push_back(cur);
    return tokens;
}

void BM25Index::add_document(const std::string& doc_id, const std::string& text) {
    auto tokens = tokenize(text);
    doc_ids_.push_back(doc_id);
    doc_lengths_.push_back((int)tokens.size());
    std::unordered_map<std::string, int> tf;
    for (auto& t : tokens) tf[t]++;
    doc_term_freqs_.push_back(std::move(tf));
}

void BM25Index::build() {
    int N = (int)doc_ids_.size();
    if (N == 0) return;
    
    // Build inverted index + DF
    inverted_.clear();
    doc_freq_.clear();
    double total_len = 0.0;
    
    for (int i = 0; i < N; ++i) {
        total_len += doc_lengths_[i];
        for (auto& [term, tf] : doc_term_freqs_[i]) {
            inverted_[term].push_back({i, tf});
            doc_freq_[term]++;
        }
    }
    avg_doc_len_ = total_len / N;
    built_ = true;
}

std::unordered_map<std::string, float> BM25Index::search(const std::string& query, int top_k) const {
    std::unordered_map<std::string, float> scores;
    if (!built_) return scores;

    auto qterms = tokenize(query);
    int N = (int)doc_ids_.size();

    for (auto& term : qterms) {
        auto inv_it = inverted_.find(term);
        if (inv_it == inverted_.end()) continue;
        
        int df = doc_freq_.at(term);
        double idf = std::log((N - df + 0.5) / (df + 0.5) + 1.0);
        if (idf <= 0.0) continue;

        for (auto& [doc_idx, tf] : inv_it->second) {
            double len_ratio = doc_lengths_[doc_idx] / avg_doc_len_;
            double bm25 = idf * (tf * (k1_ + 1.0)) / (tf + k1_ * (1.0 - b_ + b_ * len_ratio));
            scores[doc_ids_[doc_idx]] += (float)bm25;
        }
    }

    // Keep top_k
    if ((int)scores.size() > top_k && top_k > 0) {
        std::vector<std::pair<std::string, float>> sorted(scores.begin(), scores.end());
        std::partial_sort(sorted.begin(), sorted.begin() + top_k, sorted.end(),
            [](auto& a, auto& b) { return a.second > b.second; });
        scores.clear();
        for (int i = 0; i < top_k; ++i) scores[sorted[i].first] = sorted[i].second;
    }

    return scores;
}

}  // namespace hgnfs::bm25
