#include "image_file_handler.hpp"
#include "../base/file_handler_factory.hpp"
#include <fstream>
#include <sstream>

namespace zetla::file_handlers {

    static const char b64_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    static std::string base64_encode(const std::vector<uint8_t>& data) {
        std::string result;
        result.reserve(((data.size() + 2) / 3) * 4);
        for (size_t i = 0; i < data.size(); i += 3) {
            uint32_t n = static_cast<uint32_t>(data[i]) << 16;
            if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
            if (i + 2 < data.size()) n |= static_cast<uint32_t>(data[i + 2]);
            result += b64_table[(n >> 18) & 0x3F];
            result += b64_table[(n >> 12) & 0x3F];
            result += (i + 1 < data.size()) ? b64_table[(n >> 6) & 0x3F] : '=';
            result += (i + 2 < data.size()) ? b64_table[n & 0x3F] : '=';
        }
        return result;
    }

    std::string ImageFileHandler::mime_type() const { return "image/*"; }

    FileContentType ImageFileHandler::content_type() const { return FileContentType::IMAGE; }

    FileContent ImageFileHandler::extract(const std::string& file_path) {
        FileContent result;
        result.mime_type = detect_mime_type(file_path);
        result.file_name = extract_file_name(file_path);
        result.type = FileContentType::IMAGE;

        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            result.type = FileContentType::UNSUPPORTED;
            return result;
        }

        file.seekg(0, std::ios::end);
        result.size_bytes = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(result.size_bytes);
        file.read(reinterpret_cast<char*>(data.data()), data.size());

        std::string b64 = base64_encode(data);
        result.image_data_uri = "data:" + result.mime_type + ";base64," + b64;

        result.metadata["width"] = "0";
        result.metadata["height"] = "0";

        return result;
    }

}
