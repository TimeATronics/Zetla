#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <zlib.h>

namespace zetla::file_handlers::office {

    struct ZipEntry {
        std::string name;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
        uint16_t compression_method;
        uint32_t local_header_offset;
    };

    inline std::vector<uint8_t> read_all_bytes(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) return {};
        auto sz = f.tellg();
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(static_cast<size_t>(sz));
        f.read(reinterpret_cast<char*>(data.data()), sz);
        return data;
    }

    inline uint16_t read_u16(const uint8_t* p) {
        return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
    }

    inline uint32_t read_u32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    inline std::vector<ZipEntry> parse_zip_central_directory(const std::vector<uint8_t>& zip) {
        std::vector<ZipEntry> entries;
        if (zip.size() < 22) return entries;

        size_t eocd_pos = std::string::npos;
        for (size_t i = zip.size() - 22; i > (zip.size() > 65557 ? zip.size() - 65557 : 0); i--) {
            if (zip[i] == 0x50 && zip[i+1] == 0x4b && zip[i+2] == 0x05 && zip[i+3] == 0x06) {
                eocd_pos = i;
                break;
            }
        }
        if (eocd_pos == std::string::npos) return entries;

        uint32_t cd_offset = read_u32(&zip[eocd_pos + 16]);
        uint32_t cd_size = read_u32(&zip[eocd_pos + 12]);
        uint16_t num_entries = read_u16(&zip[eocd_pos + 10]);

        size_t pos = cd_offset;
        for (uint16_t i = 0; i < num_entries && pos + 46 <= cd_offset + cd_size; i++) {
            if (zip[pos] != 0x50 || zip[pos+1] != 0x4b || zip[pos+2] != 0x01 || zip[pos+3] != 0x02)
                break;

            uint16_t name_len = read_u16(&zip[pos + 28]);
            uint16_t extra_len = read_u16(&zip[pos + 30]);
            uint16_t comment_len = read_u16(&zip[pos + 32]);
            uint32_t comp_size = read_u32(&zip[pos + 20]);
            uint32_t uncomp_size = read_u32(&zip[pos + 24]);
            uint16_t method = read_u16(&zip[pos + 10]);
            uint32_t local_offset = read_u32(&zip[pos + 42]);

            if (pos + 46 + name_len > cd_offset + cd_size) break;

            std::string name(reinterpret_cast<const char*>(&zip[pos + 46]), name_len);

            entries.push_back({name, comp_size, uncomp_size, method, local_offset});
            pos += 46 + name_len + extra_len + comment_len;
        }

        return entries;
    }

    inline std::string read_zip_entry(const std::vector<uint8_t>& zip, const ZipEntry& entry) {
        size_t pos = entry.local_header_offset;
        if (pos + 30 > zip.size()) return "";

        uint16_t ln = read_u16(&zip[pos + 26]);
        uint16_t le = read_u16(&zip[pos + 28]);
        size_t data_start = pos + 30 + ln + le;

        if (data_start + entry.compressed_size > zip.size()) return "";

        const uint8_t* comp_data = &zip[data_start];

        if (entry.compression_method == 0) {
            return std::string(reinterpret_cast<const char*>(comp_data), entry.compressed_size);
        }

        if (entry.compression_method == 8) {
            uLongf dest_len = entry.uncompressed_size;
            std::vector<Bytef> dest(dest_len);
            int rc = uncompress(dest.data(), &dest_len, comp_data, static_cast<uLong>(entry.compressed_size));
            if (rc != Z_OK) return "";
            return std::string(reinterpret_cast<char*>(dest.data()), dest_len);
        }

        return "";
    }

    inline std::string find_zip_entry(const std::vector<ZipEntry>& entries, const std::string& suffix) {
        for (auto& e : entries) {
            if (e.name.size() >= suffix.size() &&
                e.name.compare(e.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                return e.name;
            }
        }
        return "";
    }

}
