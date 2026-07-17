#pragma once
#include <string>

#ifdef __ANDROID__
#include <android/log.h>
#define ZETLA_LOG_TAG "ZetlaNative"
#define ZLOGI(...) __android_log_print(ANDROID_LOG_INFO, ZETLA_LOG_TAG, __VA_ARGS__)
#define ZLOGD(...) __android_log_print(ANDROID_LOG_DEBUG, ZETLA_LOG_TAG, __VA_ARGS__)
#define ZLOGW(...) __android_log_print(ANDROID_LOG_WARN, ZETLA_LOG_TAG, __VA_ARGS__)
#define ZLOGE(...) __android_log_print(ANDROID_LOG_ERROR, ZETLA_LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define ZLOGI(fmt, ...) fprintf(stderr, "[Zetla][INFO] " fmt "\n", ##__VA_ARGS__)
#define ZLOGD(fmt, ...) fprintf(stderr, "[Zetla][DEBUG] " fmt "\n", ##__VA_ARGS__)
#define ZLOGW(fmt, ...) fprintf(stderr, "[Zetla][WARN] " fmt "\n", ##__VA_ARGS__)
#define ZLOGE(fmt, ...) fprintf(stderr, "[Zetla][ERROR] " fmt "\n", ##__VA_ARGS__)
#endif

namespace zetla::log {

    inline std::string mask_key(const std::string& key) {
        if (key.size() <= 8) return "****";
        return key.substr(0, 4) + "****" + key.substr(key.size() - 4);
    }

    inline std::string truncate(const std::string& s, size_t max_len = 500) {
        if (s.size() <= max_len) return s;
        return s.substr(0, max_len) + "... [" + std::to_string(s.size()) + " bytes total]";
    }

}
