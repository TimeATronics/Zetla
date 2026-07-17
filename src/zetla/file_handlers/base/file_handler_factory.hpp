#pragma once
#include "file_handler.hpp"
#include <memory>
#include <string>
#include <algorithm>
#include <cctype>

namespace zetla::file_handlers {

    inline std::string to_lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    inline std::string get_extension(const std::string& file_path) {
        auto dot = file_path.rfind('.');
        if (dot == std::string::npos) return "";
        return to_lower(file_path.substr(dot));
    }

    inline std::string detect_mime_type(const std::string& file_path) {
        static const std::map<std::string, std::string> ext_map = {
            {".txt",  "text/plain"},
            {".log",  "text/plain"},
            {".py",   "text/x-python"},
            {".js",   "text/javascript"},
            {".ts",   "text/typescript"},
            {".cpp",  "text/x-c++"},
            {".c",    "text/x-c"},
            {".h",    "text/x-c"},
            {".hpp",  "text/x-c++"},
            {".java", "text/x-java"},
            {".rs",   "text/x-rust"},
            {".go",   "text/x-go"},
            {".rb",   "text/x-ruby"},
            {".sh",   "text/x-shellscript"},
            {".html", "text/html"},
            {".htm",  "text/html"},
            {".css",  "text/css"},
            {".xml",  "text/xml"},
            {".yaml", "text/yaml"},
            {".yml",  "text/yaml"},
            {".toml", "text/toml"},
            {".ini",  "text/plain"},
            {".cfg",  "text/plain"},
            {".conf", "text/plain"},
            {".csv",  "text/csv"},
            {".md",   "text/markdown"},
            {".rtf",  "application/rtf"},
            {".json", "application/json"},
            {".png",  "image/png"},
            {".jpg",  "image/jpeg"},
            {".jpeg", "image/jpeg"},
            {".gif",  "image/gif"},
            {".webp", "image/webp"},
            {".bmp",  "image/bmp"},
            {".svg",  "image/svg+xml"},
            {".pdf",  "application/pdf"},
            {".doc",  "application/msword"},
            {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
            {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
            {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        };

        auto ext = get_extension(file_path);
        auto it = ext_map.find(ext);
        if (it != ext_map.end()) return it->second;
        return "application/octet-stream";
    }

    inline std::string extract_file_name(const std::string& file_path) {
        auto sep1 = file_path.rfind('/');
        auto sep2 = file_path.rfind('\\');
        auto pos = std::max(sep1, sep2);
        if (pos == std::string::npos) return file_path;
        return file_path.substr(pos + 1);
    }

    std::unique_ptr<IFileHandler> create_handler(const std::string& mime_type);

}
