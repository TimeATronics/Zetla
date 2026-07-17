#pragma once
#include "../base/storage_backend.hpp"
#include <vector>
#include <cstdint>

namespace zetla::storage {

    class Serializer {
    public:
        static bool serialize(const StoredSession& session,
                              std::vector<uint8_t>& out,
                              int compression_level = 6);

        static bool deserialize(const uint8_t* data, size_t len,
                                StoredSession& out);
    };

}
