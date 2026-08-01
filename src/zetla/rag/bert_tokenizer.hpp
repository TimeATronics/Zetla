#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace zetla::rag {

struct TokenizerOutput {
    std::vector<int64_t> input_ids;
    std::vector<int64_t> attention_mask;
    std::vector<int64_t> token_type_ids;
};

class BERTTokenizer {
public:
    BERTTokenizer() = default;

    bool load_vocab(const std::string& vocab_path);

    TokenizerOutput encode(const std::string& text, int max_length = 512) const;

private:
    std::unordered_map<std::string, int> vocab_;
    int unk_id_ = 0;
    int cls_id_ = 0;
    int sep_id_ = 0;
    int pad_id_ = 0;
};

} // namespace zetla::rag
