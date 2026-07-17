#include "file_storage_backend.hpp"
#include <fstream>
#include <algorithm>
#include <cstdlib>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(d) _mkdir(d)
#else
#include <sys/stat.h>
#include <dirent.h>
#define MKDIR(d) mkdir(d, 0755)
#endif

namespace zetla::storage {

    static std::string expand_home(const std::string& path) {
        if (path.empty() || path[0] != '~') return path;
#ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOMEDRIVE");
        if (!home) return path;
        return std::string(home) + path.substr(1);
#else
        const char* home = std::getenv("HOME");
        if (!home) return path;
        return std::string(home) + path.substr(1);
#endif
    }

    static void ensure_dir(const std::string& dir) {
        std::string expanded = expand_home(dir);
        std::string current;
        for (size_t i = 0; i < expanded.size(); i++) {
            current += expanded[i];
            if (expanded[i] == '/' || expanded[i] == '\\' || i == expanded.size() - 1) {
                if (!current.empty() && current != "/" && current != "\\") {
                    MKDIR(current.c_str());
                }
            }
        }
    }

    FileStorageBackend::FileStorageBackend(std::string base_dir)
        : base_dir_(std::move(base_dir))
    {
        if (base_dir_.empty()) {
#ifdef _WIN32
            base_dir_ = "~/.zetla/sessions";
#else
            base_dir_ = "~/.zetla/sessions";
#endif
        }
        ensure_dir(base_dir_);
    }

    std::string FileStorageBackend::file_path(const std::string& id) const {
        std::string p = expand_home(base_dir_) + "/" + id + ".bin";
#ifdef _WIN32
        std::replace(p.begin(), p.end(), '/', '\\');
#endif
        return p;
    }

    std::string FileStorageBackend::base_path() const {
        std::string p = expand_home(base_dir_);
#ifdef _WIN32
        std::replace(p.begin(), p.end(), '/', '\\');
#endif
        return p;
    }

    bool FileStorageBackend::save(const std::string& id,
                                   const uint8_t* data, size_t len)
    {
        std::string path = file_path(id);
        ensure_dir(base_dir_);

        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs.write(reinterpret_cast<const char*>(data), len);
        ofs.flush();
        bool ok = ofs.good();
        ofs.close();
        return ok;
    }

    bool FileStorageBackend::load(const std::string& id,
                                   std::vector<uint8_t>& data_out)
    {
        std::string path = file_path(id);
        std::ifstream ifs(path, std::ios::binary | std::ios::ate);
        if (!ifs.is_open()) return false;

        std::streamsize size = ifs.tellg();
        if (size <= 0) return false;
        ifs.seekg(0, std::ios::beg);

        data_out.resize(static_cast<size_t>(size));
        if (!ifs.read(reinterpret_cast<char*>(data_out.data()), size)) {
            data_out.clear();
            return false;
        }
        return true;
    }

    bool FileStorageBackend::remove(const std::string& id) {
        std::string path = file_path(id);
        return std::remove(path.c_str()) == 0;
    }

    bool FileStorageBackend::exists(const std::string& id) {
        std::string path = file_path(id);
        std::ifstream ifs(path, std::ios::binary);
        return ifs.good();
    }

    std::vector<std::string> FileStorageBackend::list_ids() {
        std::vector<std::string> ids;
        std::string dir = expand_home(base_dir_);

#ifdef _WIN32
        WIN32_FIND_DATAA find_data;
        std::string pattern = dir + "\\*.bin";
        HANDLE hFind = FindFirstFileA(pattern.c_str(), &find_data);
        if (hFind == INVALID_HANDLE_VALUE) return ids;

        do {
            std::string name = find_data.cFileName;
            if (name.size() > 4) {
                ids.push_back(name.substr(0, name.size() - 4));
            }
        } while (FindNextFileA(hFind, &find_data));

        FindClose(hFind);
#else
        DIR* d = opendir(dir.c_str());
        if (!d) return ids;

        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name.size() > 4 && name.substr(name.size() - 4) == ".bin") {
                ids.push_back(name.substr(0, name.size() - 4));
            }
        }
        closedir(d);
#endif

        return ids;
    }

}
