#include "encryption.hpp"
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <iphlpapi.h>

namespace zetla::storage {

    static std::vector<uint8_t> get_machine_id() {
        std::vector<uint8_t> id;

        char hostname[256] = {};
        DWORD hn_size = sizeof(hostname);
        GetComputerNameA(hostname, &hn_size);
        id.insert(id.end(), hostname, hostname + hn_size);

        IP_ADAPTER_INFO adapter_info[16];
        DWORD adapter_size = sizeof(adapter_info);
        if (GetAdaptersInfo(adapter_info, &adapter_size) == ERROR_SUCCESS) {
            if (adapter_size > 0) {
                id.insert(id.end(), adapter_info->Address,
                          adapter_info->Address + adapter_info->AddressLength);
            }
        }

        char username[256] = {};
        DWORD un_size = sizeof(username);
        GetUserNameA(username, &un_size);
        id.insert(id.end(), username, username + un_size);

        return id;
    }

    static const uint8_t SALT[] = {
        0x7a, 0x65, 0x74, 0x6c, 0x61, 0x5f, 0x73, 0x74,
        0x6f, 0x72, 0x61, 0x67, 0x65, 0x5f, 0x73, 0x61,
        0x6c, 0x74, 0x5f, 0x32, 0x30, 0x32, 0x36
    };

    std::vector<uint8_t> Encryption::derive_key() {
        auto machine_id = get_machine_id();

        std::vector<uint8_t> key_material;
        key_material.insert(key_material.end(), SALT, SALT + sizeof(SALT));
        key_material.insert(key_material.end(), machine_id.begin(), machine_id.end());

        BCRYPT_ALG_HANDLE hSha = nullptr;
        BCryptOpenAlgorithmProvider(&hSha, BCRYPT_SHA256_ALGORITHM, nullptr, 0);

        BCRYPT_HASH_HANDLE hHash = nullptr;
        BCryptCreateHash(hSha, &hHash, nullptr, 0, nullptr, 0, 0);
        BCryptHashData(hHash, key_material.data(),
                       static_cast<ULONG>(key_material.size()), 0);

        std::vector<uint8_t> key(KEY_SIZE);
        BCryptFinishHash(hHash, key.data(), static_cast<ULONG>(key.size()), 0);

        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hSha, 0);

        return key;
    }

    bool Encryption::encrypt(const uint8_t* key, size_t key_len,
                              const uint8_t* plaintext, size_t pt_len,
                              std::vector<uint8_t>& out)
    {
        if (key_len != KEY_SIZE) return false;

        std::vector<uint8_t> iv(IV_SIZE);
        BCryptGenRandom(nullptr, iv.data(), static_cast<ULONG>(iv.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG);

        BCRYPT_ALG_HANDLE hAlg = nullptr;
        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(status)) return false;

        status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                                   (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                                   sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
        if (!BCRYPT_SUCCESS(status)) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }

        BCRYPT_KEY_HANDLE hKey = nullptr;
        status = BCryptGenerateSymmetricKey(
            hAlg, &hKey, nullptr, 0,
            (PUCHAR)key, static_cast<ULONG>(key_len), 0);
        if (!BCRYPT_SUCCESS(status)) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }

        std::vector<uint8_t> tag_buf(TAG_SIZE, 0);

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO aead;
        BCRYPT_INIT_AUTH_MODE_INFO(aead);
        aead.pbNonce = iv.data();
        aead.cbNonce = static_cast<ULONG>(iv.size());
        aead.pbTag = tag_buf.data();
        aead.cbTag = TAG_SIZE;

        std::vector<uint8_t> ciphertext(pt_len);
        ULONG cbResult = 0;

        status = BCryptEncrypt(
            hKey,
            (PUCHAR)plaintext, static_cast<ULONG>(pt_len),
            &aead,
            nullptr, 0,
            ciphertext.data(), static_cast<ULONG>(ciphertext.size()),
            &cbResult,
            0);

        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);

        if (!BCRYPT_SUCCESS(status)) return false;

        out.resize(IV_SIZE + TAG_SIZE + ciphertext.size());
        std::memcpy(out.data(), iv.data(), IV_SIZE);
        std::memcpy(out.data() + IV_SIZE, tag_buf.data(), TAG_SIZE);
        std::memcpy(out.data() + IV_SIZE + TAG_SIZE, ciphertext.data(), ciphertext.size());

        return true;
    }

    bool Encryption::decrypt(const uint8_t* key, size_t key_len,
                              const uint8_t* ciphertext, size_t ct_len,
                              std::vector<uint8_t>& out)
    {
        if (key_len != KEY_SIZE || ct_len < IV_SIZE + TAG_SIZE) return false;

        const uint8_t* iv = ciphertext;
        const uint8_t* tag = ciphertext + IV_SIZE;
        const uint8_t* enc_data = ciphertext + IV_SIZE + TAG_SIZE;
        size_t enc_len = ct_len - IV_SIZE - TAG_SIZE;

        BCRYPT_ALG_HANDLE hAlg = nullptr;
        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(status)) return false;

        status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                                   (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                                   sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
        if (!BCRYPT_SUCCESS(status)) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }

        BCRYPT_KEY_HANDLE hKey = nullptr;
        status = BCryptGenerateSymmetricKey(
            hAlg, &hKey, nullptr, 0,
            (PUCHAR)key, static_cast<ULONG>(key_len), 0);
        if (!BCRYPT_SUCCESS(status)) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO aead;
        BCRYPT_INIT_AUTH_MODE_INFO(aead);
        aead.pbNonce = (PUCHAR)iv;
        aead.cbNonce = IV_SIZE;
        aead.pbTag = (PUCHAR)tag;
        aead.cbTag = TAG_SIZE;

        out.resize(enc_len);
        ULONG cbResult = 0;

        status = BCryptDecrypt(
            hKey,
            (PUCHAR)enc_data, static_cast<ULONG>(enc_len),
            &aead,
            nullptr, 0,
            out.data(), static_cast<ULONG>(out.size()),
            &cbResult,
            0);

        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);

        if (!BCRYPT_SUCCESS(status)) {
            out.clear();
            return false;
        }

        out.resize(cbResult);
        return true;
    }

}

#else

#include <unistd.h>
#include <pwd.h>
#include <sys/socket.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <cstring>

#include <openssl/evp.h>
#include <openssl/rand.h>

#ifdef __linux__
#include <netpacket/packet.h>
#endif

#ifdef __APPLE__
#include <net/if_dl.h>
#endif

namespace zetla::storage {

    static std::vector<uint8_t> get_machine_id() {
        std::vector<uint8_t> id;

        char hostname[256] = {};
        if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
            id.insert(id.end(), hostname, hostname + std::strlen(hostname));
        }

        struct ifaddrs *ifaddr = nullptr;
        if (getifaddrs(&ifaddr) == 0) {
            for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == nullptr) continue;

                const unsigned char *mac = nullptr;
                size_t mac_len = 0;

#ifdef __linux__
                if (ifa->ifa_addr->sa_family == AF_PACKET) {
                    struct sockaddr_ll *sll = reinterpret_cast<struct sockaddr_ll*>(ifa->ifa_addr);
                    if (sll->sll_halen == 6) {
                        mac = sll->sll_addr;
                        mac_len = 6;
                    }
                }
#elif defined(__APPLE__)
                if (ifa->ifa_addr->sa_family == AF_LINK) {
                    struct sockaddr_dl *sdl = reinterpret_cast<struct sockaddr_dl*>(ifa->ifa_addr);
                    if (sdl->sdl_alen >= 6) {
                        mac = reinterpret_cast<const unsigned char*>(sdl->sdl_data + sdl->sdl_nlen);
                        mac_len = 6;
                    }
                }
#endif

                if (mac) {
                    id.insert(id.end(), mac, mac + mac_len);
                    break;
                }
            }
            freeifaddrs(ifaddr);
        }

        struct passwd *pw = getpwuid(getuid());
        if (pw && pw->pw_name) {
            std::string user(pw->pw_name);
            id.insert(id.end(), user.begin(), user.end());
        }

        return id;
    }

    static const uint8_t SALT[] = {
        0x7a, 0x65, 0x74, 0x6c, 0x61, 0x5f, 0x73, 0x74,
        0x6f, 0x72, 0x61, 0x67, 0x65, 0x5f, 0x73, 0x61,
        0x6c, 0x74, 0x5f, 0x32, 0x30, 0x32, 0x36
    };

    std::vector<uint8_t> Encryption::derive_key() {
        auto machine_id = get_machine_id();

        std::vector<uint8_t> key_material;
        key_material.insert(key_material.end(), SALT, SALT + sizeof(SALT));
        key_material.insert(key_material.end(), machine_id.begin(), machine_id.end());

        std::vector<uint8_t> key(KEY_SIZE);
        unsigned int len = 0;
        EVP_Digest(key_material.data(), key_material.size(),
                   key.data(), &len, EVP_sha256(), nullptr);

        return key;
    }

    bool Encryption::encrypt(const uint8_t* key, size_t key_len,
                              const uint8_t* plaintext, size_t pt_len,
                              std::vector<uint8_t>& out)
    {
        if (key_len != KEY_SIZE) return false;

        std::vector<uint8_t> iv(IV_SIZE);
        if (RAND_bytes(iv.data(), IV_SIZE) != 1) return false;

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return false;

        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        std::vector<uint8_t> ciphertext(pt_len);
        int len = 0;
        int total_len = 0;

        if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext, pt_len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        total_len = len;

        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + total_len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        total_len += len;
        ciphertext.resize(total_len);

        std::vector<uint8_t> tag(TAG_SIZE);
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        EVP_CIPHER_CTX_free(ctx);

        out.resize(IV_SIZE + TAG_SIZE + ciphertext.size());
        std::memcpy(out.data(), iv.data(), IV_SIZE);
        std::memcpy(out.data() + IV_SIZE, tag.data(), TAG_SIZE);
        std::memcpy(out.data() + IV_SIZE + TAG_SIZE, ciphertext.data(), ciphertext.size());

        return true;
    }

    bool Encryption::decrypt(const uint8_t* key, size_t key_len,
                              const uint8_t* ciphertext, size_t ct_len,
                              std::vector<uint8_t>& out)
    {
        if (key_len != KEY_SIZE || ct_len < IV_SIZE + TAG_SIZE) return false;

        const uint8_t* iv = ciphertext;
        const uint8_t* tag = ciphertext + IV_SIZE;
        const uint8_t* enc_data = ciphertext + IV_SIZE + TAG_SIZE;
        size_t enc_len = ct_len - IV_SIZE - TAG_SIZE;

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return false;

        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, iv) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        out.resize(enc_len);
        int len = 0;
        int total_len = 0;

        if (EVP_DecryptUpdate(ctx, out.data(), &len, enc_data, enc_len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            out.clear();
            return false;
        }
        total_len = len;

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE,
                                const_cast<void*>(static_cast<const void*>(tag))) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            out.clear();
            return false;
        }

        int ret = EVP_DecryptFinal_ex(ctx, out.data() + total_len, &len);
        EVP_CIPHER_CTX_free(ctx);

        if (ret != 1) {
            out.clear();
            return false;
        }

        total_len += len;
        out.resize(total_len);
        return true;
    }

}
#endif
