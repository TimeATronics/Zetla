#pragma once
#include <string>
#include <optional>
#include "nlohmann/json.hpp"

namespace zetla::json {

    inline std::string escape(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                case '\b': out += "\\b";  break;
                case '\f': out += "\\f";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        static const char* hex = "0123456789abcdef";
                        out += "\\u00";
                        out += hex[(static_cast<unsigned char>(c) >> 4) & 0xf];
                        out += hex[static_cast<unsigned char>(c) & 0xf];
                    } else {
                        out += c;
                    }
                    break;
            }
        }
        return out;
    }

    inline std::optional<nlohmann::json> try_parse(const std::string& s) {
        try {
            return nlohmann::json::parse(s);
        } catch (...) {
            return std::nullopt;
        }
    }

    inline std::string extract_string(const std::string& json, const std::string& key) {
        auto j = try_parse(json);
        if (!j) return {};
        try {
            auto& v = (*j)[key];
            if (v.is_string()) return v.get<std::string>();
            return {};
        } catch (...) {
            return {};
        }
    }

    inline std::string extract_raw_value(const std::string& json, const std::string& key) {
        auto j = try_parse(json);
        if (!j) return {};
        try {
            auto& v = (*j)[key];
            if (v.is_string()) return v.get<std::string>();
            if (v.is_number()) return std::to_string(v.get<double>());
            if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
            return v.dump();
        } catch (...) {
            return {};
        }
    }

    inline void extract_delta_content(const std::string& sse_data, std::string& content_out, std::string& reasoning_out) {
        content_out.clear();
        reasoning_out.clear();

        auto j = try_parse(sse_data);
        if (!j) return;

        try {
            auto& choices = (*j)["choices"];
            if (choices.is_array() && !choices.empty()) {
                auto& choice = choices[0];
                if (choice.contains("delta")) {
                    auto& delta = choice["delta"];
                    if (delta.contains("content") && delta["content"].is_string()) {
                        content_out = delta["content"].get<std::string>();
                    }
                    if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
                        reasoning_out = delta["reasoning_content"].get<std::string>();
                    }
                }
            }
        } catch (...) {}
    }

}
