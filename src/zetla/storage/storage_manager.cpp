#include "storage_manager.hpp"

namespace zetla::storage {

    StorageManager::StorageManager(std::unique_ptr<IStorageBackend> backend)
        : backend_(std::move(backend)) {}

    bool StorageManager::save_session(const StoredSession& session) {
        std::vector<uint8_t> compressed;
        if (!Serializer::serialize(session, compressed)) return false;

        auto key = Encryption::derive_key();
        std::vector<uint8_t> encrypted;
        if (!Encryption::encrypt(key.data(), key.size(),
                                 compressed.data(), compressed.size(),
                                 encrypted)) {
            return false;
        }

        return backend_->save(session.id, encrypted.data(), encrypted.size());
    }

    bool StorageManager::load_session(const std::string& id, StoredSession& out) {
        std::vector<uint8_t> encrypted;
        if (!backend_->load(id, encrypted)) return false;

        auto key = Encryption::derive_key();
        std::vector<uint8_t> compressed;
        if (!Encryption::decrypt(key.data(), key.size(),
                                 encrypted.data(), encrypted.size(),
                                 compressed)) {
            return false;
        }

        return Serializer::deserialize(compressed.data(), compressed.size(), out);
    }

    bool StorageManager::delete_session(const std::string& id) {
        return backend_->remove(id);
    }

    bool StorageManager::session_exists(const std::string& id) {
        return backend_->exists(id);
    }

    std::vector<std::string> StorageManager::list_session_ids() {
        return backend_->list_ids();
    }

    std::vector<StoredSession> StorageManager::list_all_sessions() {
        auto ids = list_session_ids();
        std::vector<StoredSession> sessions;
        for (auto& id : ids) {
            StoredSession s;
            if (load_session(id, s)) {
                sessions.push_back(std::move(s));
            }
        }
        return sessions;
    }

    std::string StorageManager::base_path() const {
        return backend_->base_path();
    }

}
