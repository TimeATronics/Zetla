#include "pdf_file_handler.hpp"
#include "../base/file_handler_factory.hpp"
#include <fstream>
#include <sstream>
#include <vector>
#include <zlib.h>

namespace zetla::file_handlers {

    std::string PdfFileHandler::mime_type() const { return "application/pdf"; }

    FileContentType PdfFileHandler::content_type() const { return FileContentType::TEXT; }

    static std::vector<uint8_t> read_file_bytes(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        auto sz = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(static_cast<size_t>(sz));
        file.read(reinterpret_cast<char*>(data.data()), sz);
        return data;
    }

    static std::string flate_decode(const uint8_t* data, size_t len) {
        uLongf dest_len = len * 8;
        std::vector<Bytef> dest(dest_len);
        int rc = uncompress(dest.data(), &dest_len, data, static_cast<uLong>(len));
        if (rc != Z_OK) return "";
        return std::string(reinterpret_cast<char*>(dest.data()), dest_len);
    }

    static std::string extract_text_from_pdf_stream(const std::string& decompressed) {
        std::string result;
        size_t i = 0;
        while (i < decompressed.size()) {
            if (decompressed[i] == '(') {
                i++;
                while (i < decompressed.size() && decompressed[i] != ')') {
                    if (decompressed[i] == '\\' && i + 1 < decompressed.size()) {
                        i++;
                        char esc = decompressed[i];
                        switch (esc) {
                            case 'n': result += '\n'; break;
                            case 'r': result += '\r'; break;
                            case 't': result += '\t'; break;
                            case 'b': result += '\b'; break;
                            case 'f': result += '\f'; break;
                            case '\\': result += '\\'; break;
                            case '(': result += '('; break;
                            case ')': result += ')'; break;
                            default:
                                if (esc >= '0' && esc <= '7') {
                                    int octal = esc - '0';
                                    int digits = 1;
                                    while (digits < 3 && i + 1 < decompressed.size()) {
                                        char next = decompressed[i + 1];
                                        if (next >= '0' && next <= '7') {
                                            i++;
                                            octal = octal * 8 + (next - '0');
                                            digits++;
                                        } else break;
                                    }
                                    result += static_cast<char>(octal);
                                } else {
                                    result += esc;
                                }
                                break;
                        }
                    } else {
                        result += decompressed[i];
                    }
                    i++;
                }
                if (i < decompressed.size()) i++;
            } else {
                i++;
            }
        }
        return result;
    }

    static bool has_flate_decode(const std::string& dict) {
        return dict.find("/FlateDecode") != std::string::npos;
    }

    static bool has_image_filter(const std::string& dict) {
        return dict.find("/DCTDecode") != std::string::npos ||
               dict.find("/JPXDecode") != std::string::npos;
    }

    FileContent PdfFileHandler::extract(const std::string& file_path) {
        FileContent result;
        result.mime_type = "application/pdf";
        result.file_name = extract_file_name(file_path);
        result.type = FileContentType::TEXT;

        auto raw = read_file_bytes(file_path);
        if (raw.empty()) {
            result.type = FileContentType::UNSUPPORTED;
            return result;
        }

        result.size_bytes = raw.size();

        std::string all_text;
        size_t page_count = 0;
        size_t image_count = 0;

        for (size_t i = 0; i + 6 < raw.size(); i++) {
            if (raw[i] == 's' && raw[i+1] == 't' && raw[i+2] == 'r' &&
                raw[i+3] == 'e' && raw[i+4] == 'a' && raw[i+5] == 'm') {
                size_t stream_start = i + 6;
                while (stream_start < raw.size() && raw[stream_start] != '\n' && raw[stream_start] != '\r')
                    stream_start++;
                while (stream_start < raw.size() && (raw[stream_start] == '\n' || raw[stream_start] == '\r'))
                    stream_start++;

                size_t endstream_pos = std::string::npos;
                for (size_t j = stream_start; j + 9 < raw.size(); j++) {
                    if (raw[j] == 'e' && raw[j+1] == 'n' && raw[j+2] == 'd' &&
                        raw[j+3] == 's' && raw[j+4] == 't' && raw[j+5] == 'r' &&
                        raw[j+6] == 'e' && raw[j+7] == 'a' && raw[j+8] == 'm') {
                        endstream_pos = j;
                        break;
                    }
                }
                if (endstream_pos == std::string::npos) continue;

                size_t dict_start = (i > 256) ? i - 256 : 0;
                std::string dict(reinterpret_cast<const char*>(&raw[dict_start]), i - dict_start);

                size_t stream_len = endstream_pos - stream_start;

                if (has_flate_decode(dict)) {
                    std::string decompressed = flate_decode(&raw[stream_start], stream_len);
                    if (!decompressed.empty()) {
                        if (has_image_filter(dict)) {
                            image_count++;
                        } else {
                            std::string text = extract_text_from_pdf_stream(decompressed);
                            if (!text.empty()) {
                                all_text += text;
                                all_text += '\n';
                            }
                        }
                    }
                } else if (!has_image_filter(dict)) {
                    std::string raw_stream(reinterpret_cast<const char*>(&raw[stream_start]), stream_len);
                    std::string text = extract_text_from_pdf_stream(raw_stream);
                    if (!text.empty()) {
                        all_text += text;
                        all_text += '\n';
                    }
                }

                i = endstream_pos + 8;
            }

            if (raw[i] == '/' && i + 4 < raw.size() &&
                raw[i+1] == 'T' && raw[i+2] == 'y' && raw[i+3] == 'p' && raw[i+4] == 'e') {
                size_t j = i + 5;
                while (j < raw.size() && raw[j] != '/' && raw[j] != '\n' && raw[j] != '\r') j++;
                if (j > i + 5) {
                    std::string val(reinterpret_cast<const char*>(&raw[i+5]), j - i - 5);
                    if (val.find("/Page") != std::string::npos) page_count++;
                }
            }
        }

        if (all_text.empty()) {
            all_text = "[PDF content - text extraction yielded no readable text. "
                       "The PDF may contain primarily images or use non-standard encoding.]";
        }

        size_t trimmed = all_text.find_last_not_of(" \n\r\t");
        if (trimmed != std::string::npos) all_text = all_text.substr(0, trimmed + 1);

        result.text_content = std::move(all_text);
        result.metadata["approximate_pages"] = std::to_string(page_count > 0 ? page_count : 0);
        result.metadata["embedded_images"] = std::to_string(image_count);

        return result;
    }

}
