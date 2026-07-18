#include "pdf_file_handler.hpp"
#include "../base/file_handler_factory.hpp"
#include <fstream>
#include <vector>

namespace zetla::file_handlers {

    std::string PdfFileHandler::mime_type() const { return "application/pdf"; }

    FileContentType PdfFileHandler::content_type() const { return FileContentType::TEXT; }

    FileContent PdfFileHandler::extract(const std::string& file_path) {
        FileContent result;
        result.mime_type = "application/pdf";
        result.file_name = extract_file_name(file_path);
        result.type = FileContentType::TEXT;

        std::ifstream f(file_path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) {
            result.type = FileContentType::UNSUPPORTED;
            result.text_content = "Failed to open PDF file.";
            return result;
        }
        auto sz = f.tellg();
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(static_cast<size_t>(sz));
        f.read(reinterpret_cast<char*>(data.data()), sz);
        result.size_bytes = data.size();
        // PDF text extraction is now done on the Kotlin side using PDFBox-Android
        // The Kotlin side receives the file path, reads it via ContentResolver,
        // extracts text using PDFBox, and decides whether to include images
        // based on the selected model's vision capability.
        result.text_content = "[PDF file attached - text extraction will be performed on the device.]";
        result.metadata["extraction"] = "kotlin_pdfbox";
        result.metadata["approximate_pages"] = "0";
        result.metadata["embedded_images"] = "0";

        return result;
    }

}
