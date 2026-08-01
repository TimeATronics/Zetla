#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace hgnfs::bm25 {

// Simple BM25 inverted index for hybrid search
class BM25Index {
public:
    void add_document(const std::string& doc_id, const std::string& text);
    void build();
    
    // Returns {doc_id -> BM25 score}
    std::unordered_map<std::string, float> search(const std::string& query, int top_k = 100) const;

    int doc_count() const { return (int)doc_lengths_.size(); }

private:
    double k1_ = 1.2, b_ = 0.75;
    std::vector<std::string> doc_ids_;
    std::vector<int> doc_lengths_;
    std::vector<std::unordered_map<std::string, int>> doc_term_freqs_;  // doc_id -> {term -> tf}
    
    // Inverted index: term -> {doc_index -> tf}
    std::unordered_map<std::string, std::vector<std::pair<int, int>>> inverted_;
    std::unordered_map<std::string, int> doc_freq_;  // term -> df
    double avg_doc_len_ = 1.0;
    bool built_ = false;
};

}  // namespace hgnfs::bm25
