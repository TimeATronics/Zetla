#include "../base/file_handler_factory.hpp"
#include "../text/text_file_handler.hpp"
#include "../image/image_file_handler.hpp"
#include "../pdf/pdf_file_handler.hpp"
#include "../office/office_file_handler.hpp"

namespace zetla::file_handlers {

    std::unique_ptr<IFileHandler> create_handler(const std::string& mime_type) {
        std::string mt = to_lower(mime_type);

        if (mt == "application/pdf") {
            return std::make_unique<PdfFileHandler>();
        }

        if (mt.size() >= 6 && mt.substr(0, 6) == "image/") {
            return std::make_unique<ImageFileHandler>();
        }

        if (mt == "application/vnd.openxmlformats-officedocument.wordprocessingml.document" ||
            mt == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" ||
            mt == "application/vnd.openxmlformats-officedocument.presentationml.presentation") {
            return std::make_unique<OfficeFileHandler>();
        }

        if (mt == "text/csv") {
            return std::make_unique<OfficeFileHandler>();
        }

        if (mt == "text/markdown") {
            return std::make_unique<OfficeFileHandler>();
        }

        if (mt == "application/rtf") {
            return std::make_unique<OfficeFileHandler>();
        }

        if (mt.size() >= 5 && mt.substr(0, 5) == "text/") {
            return std::make_unique<TextFileHandler>();
        }

        if (mt == "application/json" || mt == "application/xml" ||
            mt == "application/javascript" || mt == "application/x-yaml") {
            return std::make_unique<TextFileHandler>();
        }

        return std::make_unique<TextFileHandler>();
    }

}
