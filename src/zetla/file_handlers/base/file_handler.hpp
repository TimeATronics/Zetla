#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>

namespace zetla::file_handlers {

    enum class FileContentType {
        TEXT,
        IMAGE,
        STRUCTURED,
        UNSUPPORTED
    };

    struct FileContent {
        FileContentType type = FileContentType::UNSUPPORTED;
        std::string text_content;
        std::string image_data_uri;
        std::string mime_type;
        std::string file_name;
        size_t size_bytes = 0;
        std::map<std::string, std::string> metadata;
    };

    struct RegisteredFile {
        std::string file_id;
        std::string path;
        std::string name;
        std::string mime_type;
        FileContent content;
        size_t size_bytes = 0;
    };

    class IFileHandler {
    public:
        virtual ~IFileHandler() = default;
        virtual std::string mime_type() const = 0;
        virtual FileContent extract(const std::string& file_path) = 0;
        virtual FileContentType content_type() const = 0;
    };

    inline std::string content_type_name(FileContentType t) {
        switch (t) {
            case FileContentType::TEXT: return "text";
            case FileContentType::IMAGE: return "image";
            case FileContentType::STRUCTURED: return "structured";
            case FileContentType::UNSUPPORTED: return "unsupported";
        }
        return "unknown";
    }

}
