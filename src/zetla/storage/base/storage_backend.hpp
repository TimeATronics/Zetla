#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace zetla::storage {

    struct StoredSession {
        std::string id;
        std::string model;
        std::string title;
        bool is_starred = false;
        bool is_space = false;
        std::string system_prompt;
        std::vector<std::pair<std::string, std::string>> messages;  // role, content
        std::string options_json;     // serialized ChatOptions
        std::string compacted_summary;
        std::string space_files_json; // JSON array of {name, path, added_at_ms}
        int64_t created_at_ms = 0;
        int64_t last_active_ms = 0;
    };

    class IStorageBackend {
    public:
        virtual ~IStorageBackend() = default;

        virtual bool save(const std::string& id,
                          const uint8_t* data, size_t len) = 0;

        virtual bool load(const std::string& id,
                          std::vector<uint8_t>& data_out) = 0;

        virtual bool remove(const std::string& id) = 0;

        virtual bool exists(const std::string& id) = 0;

        virtual std::vector<std::string> list_ids() = 0;

        virtual std::string base_path() const = 0;
    };

}
