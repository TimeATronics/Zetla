#include "chunker.hpp"
#include <sstream>

namespace hgnfs::chunker {

std::vector<Chunk> chunk_text(const std::string& text,
                              int chunk_words, int overlap_words,
                              const std::string& section_prefix) {
    std::vector<Chunk> results;
    if (text.empty()) return results;

    std::vector<std::string> words;
    std::istringstream iss(text);
    std::string w;
    while (iss >> w) words.push_back(w);

    int n = (int)words.size();
    if (n <= chunk_words) {
        std::string c = section_prefix.empty() ? text : section_prefix + ": " + text;
        results.push_back({c, 0});
        return results;
    }

    int step = chunk_words - overlap_words;
    if (step <= 0) step = 1;
    int idx = 0;
    for (int start = 0; start < n; start += step) {
        std::string chunk;
        for (int i = start; i < start + chunk_words && i < n; ++i) {
            if (!chunk.empty()) chunk += ' ';
            chunk += words[i];
        }
        if (!chunk.empty()) {
            std::string full = section_prefix.empty() ? chunk : section_prefix + ": " + chunk;
            results.push_back({full, idx++});
        }
    }
    return results;
}

}  // namespace hgnfs::chunker
