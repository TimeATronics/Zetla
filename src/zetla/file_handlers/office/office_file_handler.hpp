#pragma once
#include "../base/file_handler.hpp"

namespace zetla::file_handlers {

    class OfficeFileHandler : public IFileHandler {
    public:
        std::string mime_type() const override;
        FileContent extract(const std::string& file_path) override;
        FileContentType content_type() const override;

    private:
        FileContent extract_xlsx(const std::string& file_path, const std::string& name);
        FileContent extract_docx(const std::string& file_path, const std::string& name);
        FileContent extract_pptx(const std::string& file_path, const std::string& name);
        FileContent extract_csv(const std::string& file_path, const std::string& name);
        FileContent extract_markdown(const std::string& file_path, const std::string& name);
        FileContent extract_rtf(const std::string& file_path, const std::string& name);
    };

}
