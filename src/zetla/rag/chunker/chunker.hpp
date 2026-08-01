#pragma once
#include <string>
#include <vector>

namespace hgnfs::chunker {

struct Chunk {
    std::string text;
    int index;
};

std::vector<Chunk> chunk_text(const std::string& text,
                              int chunk_words = 300,
                              int overlap_words = 60,
                              const std::string& section_prefix = "");

}  // namespace hgnfs::chunker
