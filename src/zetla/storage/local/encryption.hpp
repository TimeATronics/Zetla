#pragma once
#include <vector>
#include <cstdint>
#include <string>

namespace zetla::storage {

    class Encryption {
    public:
        static std::vector<uint8_t> derive_key();

        static bool encrypt(const uint8_t* key, size_t key_len,
                            const uint8_t* plaintext, size_t pt_len,
                            std::vector<uint8_t>& out);

        static bool decrypt(const uint8_t* key, size_t key_len,
                            const uint8_t* ciphertext, size_t ct_len,
                            std::vector<uint8_t>& out);

        static constexpr size_t KEY_SIZE = 32;
        static constexpr size_t IV_SIZE = 12;
        static constexpr size_t TAG_SIZE = 16;
    };

}
