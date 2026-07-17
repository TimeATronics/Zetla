#pragma once
#include "../base/file_handler.hpp"

namespace zetla::file_handlers {

    class ImageFileHandler : public IFileHandler {
    public:
        std::string mime_type() const override;
        FileContent extract(const std::string& file_path) override;
        FileContentType content_type() const override;
    };

}
