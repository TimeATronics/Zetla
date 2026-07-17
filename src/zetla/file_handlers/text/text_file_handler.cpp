#include "text_file_handler.hpp"
#include "../base/file_handler_factory.hpp"
#include <fstream>
#include <sstream>

namespace zetla::file_handlers {

    std::string TextFileHandler::mime_type() const { return "text/*"; }

    FileContentType TextFileHandler::content_type() const { return FileContentType::TEXT; }

    FileContent TextFileHandler::extract(const std::string& file_path) {
        FileContent result;
        result.mime_type = detect_mime_type(file_path);
        result.file_name = extract_file_name(file_path);

        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            result.type = FileContentType::UNSUPPORTED;
            return result;
        }

        file.seekg(0, std::ios::end);
        result.size_bytes = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::ostringstream ss;
        ss << file.rdbuf();
        result.text_content = ss.str();

        if (result.mime_type == "application/json" || result.mime_type == "text/csv") {
            result.type = FileContentType::STRUCTURED;
        } else {
            result.type = FileContentType::TEXT;
        }

        size_t lines = 0;
        for (char c : result.text_content) {
            if (c == '\n') lines++;
        }
        result.metadata["line_count"] = std::to_string(lines);

        return result;
    }

}
