#pragma once
#include "base/storage_backend.hpp"
#include "local/serialization.hpp"
#include "local/encryption.hpp"
#include <memory>
#include <string>

namespace zetla::storage {

    class StorageManager {
    private:
        std::unique_ptr<IStorageBackend> backend_;

    public:
        explicit StorageManager(std::unique_ptr<IStorageBackend> backend);

        bool save_session(const StoredSession& session);

        bool load_session(const std::string& id, StoredSession& out);

        bool delete_session(const std::string& id);

        bool session_exists(const std::string& id);

        std::vector<std::string> list_session_ids();

        std::vector<StoredSession> list_all_sessions();

        std::string base_path() const;
    };

}
