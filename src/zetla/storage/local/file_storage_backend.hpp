#pragma once
#include "../base/storage_backend.hpp"
#include <string>

namespace zetla::storage {

    class FileStorageBackend : public IStorageBackend {
    private:
        std::string base_dir_;

    public:
        explicit FileStorageBackend(std::string base_dir = "");

        bool save(const std::string& id,
                  const uint8_t* data, size_t len) override;

        bool load(const std::string& id,
                  std::vector<uint8_t>& data_out) override;

        bool remove(const std::string& id) override;

        bool exists(const std::string& id) override;

        std::vector<std::string> list_ids() override;

        std::string base_path() const override;

    private:
        std::string file_path(const std::string& id) const;
    };

}
