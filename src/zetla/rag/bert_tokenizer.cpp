#include "bert_tokenizer.hpp"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace zetla::rag {

bool BERTTokenizer::load_vocab(const std::string& vocab_path) {
    std::ifstream file(vocab_path);
    if (!file.is_open()) return false;

    vocab_.clear();
    std::string token;
    int id = 0;
    while (std::getline(file, token)) {
        if (!token.empty() && token.back() == '\r') token.pop_back();
        vocab_[token] = id++;
    }

    auto it = vocab_.find("[UNK]"); if (it != vocab_.end()) unk_id_ = it->second;
    it = vocab_.find("[CLS]");      if (it != vocab_.end()) cls_id_ = it->second;
    it = vocab_.find("[SEP]");      if (it != vocab_.end()) sep_id_ = it->second;
    it = vocab_.find("[PAD]");      if (it != vocab_.end()) pad_id_ = it->second;

    return !vocab_.empty();
}

static std::string lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out += (char)std::tolower((unsigned char)c);
    return out;
}

static std::vector<std::string> split_on_whitespace(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : text) {
        if (std::isspace((unsigned char)c)) {
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
        } else {
            current += c;
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

TokenizerOutput BERTTokenizer::encode(const std::string& text, int max_length) const {
    TokenizerOutput out;
    if (vocab_.empty()) return out;

    std::string ltext = lower(text);
    auto words = split_on_whitespace(ltext);

    std::vector<int64_t> ids;
    ids.reserve(std::min((int)words.size() * 3 + 2, max_length));
    ids.push_back(cls_id_);

    for (const auto& word : words) {
        if ((int)ids.size() >= max_length - 1) break;

        // Try exact match first
        auto wit = vocab_.find(word);
        if (wit != vocab_.end()) {
            ids.push_back(wit->second);
            continue;
        }

        // WordPiece: greedy longest-prefix matching (reuse buffer)
        const char* wp = word.c_str();
        const char* we = wp + word.size();
        bool first = true;
        std::string sbuf; sbuf.reserve(64);

        while (wp < we && (int)ids.size() < max_length - 1) {
            int best_len = 0;
            int best_id = -1;

            for (int l = (int)(we - wp); l >= 1; --l) {
                sbuf.clear();
                if (!first) sbuf.append("##");
                sbuf.append(wp, l);
                auto it = vocab_.find(sbuf);
                if (it != vocab_.end()) { best_len = l; best_id = it->second; break; }
            }

            if (best_id >= 0) {
                ids.push_back(best_id);
                wp += best_len;
                first = false;
            } else {
                ids.push_back(unk_id_);
                break;
            }
        }
    }

    if ((int)ids.size() < max_length) ids.push_back(sep_id_);

    int seq_len = (int)ids.size();
    ids.resize(max_length, pad_id_);

    out.input_ids = ids;
    out.attention_mask.resize(max_length, 0);
    for (int i = 0; i < seq_len && i < max_length; ++i)
        out.attention_mask[i] = 1;
    out.token_type_ids.resize(max_length, 0);

    return out;
}

} // namespace zetla::rag
