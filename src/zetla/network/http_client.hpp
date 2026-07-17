#pragma once
#include <string>
#include <functional>
#include <curl/curl.h>
#include <atomic>
#include "../core/log.hpp"

namespace zetla::network {

    using StreamCallback = std::function<void(const std::string& data)>;

    struct HttpRequest {
        std::string url;
        std::string body;
        std::string api_key;
        std::string method = "POST";
        bool stream = false;
        int timeout_seconds = 120;
        bool verify_ssl = true;
        std::string user_agent;
    };

    struct HttpResponse {
        int status_code = 0;
        std::string body;
        std::string error;
        bool success() const { return status_code >= 200 && status_code < 400; }
    };

    class HttpClient {
    public:
        static int abort_progress_callback(void* clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
            return g_abort_flag.load() ? 1 : 0;
        }

        static void global_init() {
            static std::atomic<bool> initialized{false};
            if (!initialized.exchange(true)) {
                curl_global_init(CURL_GLOBAL_DEFAULT);
            }
        }

        static void global_cleanup() {
            curl_global_cleanup();
        }

        static void request_cancel() {
            g_abort_flag.store(true);
        }

        static void request_reset() {
            g_abort_flag.store(false);
        }

        static bool is_cancelled() {
            return g_abort_flag.load();
        }

        static HttpResponse post(const HttpRequest& req) {
            HttpRequest r = req;
            r.verify_ssl = false;
            return execute(r);
        }

        static HttpResponse get(const std::string& url, const std::string& api_key) {
            HttpRequest req;
            req.url = url;
            req.api_key = api_key;
            req.method = "GET";
            req.verify_ssl = false;
            req.user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
            return execute(req);
        }

        static bool get_sync(
            const std::string& url,
            const std::string& api_key,
            std::string& response_out,
            std::string& error_out,
            bool verify_ssl = false
        ) {
            CURL* curl = curl_easy_init();
            if (!curl) {
                error_out = "Failed to init curl";
                return false;
            }

            struct curl_slist* headers = nullptr;
            if (!api_key.empty()) {
                std::string auth_header = "Authorization: Bearer " + api_key;
                headers = curl_slist_append(headers, auth_header.c_str());
            }
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sync_write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_out);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

            if (!verify_ssl) {
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            }

            CURLcode res = curl_easy_perform(curl);
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

            bool ok = (res == CURLE_OK && http_code >= 200 && http_code < 400);
            if (!ok) {
                if (res != CURLE_OK) {
                    error_out = curl_easy_strerror(res);
                } else {
                    error_out = "HTTP " + std::to_string(http_code);
                }
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return ok;
        }

        static bool post_stream(
            const std::string& url,
            const std::string& body,
            const std::string& api_key,
            StreamCallback on_data,
            std::string& error_out,
            bool verify_ssl = false
        ) {
            CURL* curl = curl_easy_init();
            if (!curl) {
                error_out = "Failed to init curl";
                ZLOGE("post_stream: Failed to init curl");
                return false;
            }

            ZLOGI("post_stream: URL=%s body=%s", url.c_str(), log::truncate(body, 1000).c_str());
            ZLOGI("post_stream: api_key=%s", log::mask_key(api_key).c_str());

            g_abort_flag.store(false);
            std::string full_url = url;
            std::string error_body;

            struct curl_slist* headers = nullptr;
            if (!api_key.empty()) {
                std::string auth_header = "Authorization: Bearer " + api_key;
                headers = curl_slist_append(headers, auth_header.c_str());
            }
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, "Accept: application/json, text/event-stream");

            curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            struct StreamCapture capture;
            capture.user_cb = &on_data;

            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &capture);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &capture);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

            if (!verify_ssl) {
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            }

            CURLcode res = curl_easy_perform(curl);

            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (capture.http_code > 0) http_code = capture.http_code;

            bool ok = (res == CURLE_OK && http_code < 400);

            if (!ok) {
                if (g_abort_flag.load()) {
                    error_out = "";
                    ZLOGI("post_stream: request cancelled by user");
                } else if (res != CURLE_OK) {
                    error_out = curl_easy_strerror(res);
                    ZLOGE("post_stream: CURL error=%s", error_out.c_str());
                } else {
                    error_out = "HTTP " + std::to_string(http_code) + ": " + capture.error_body;
                    ZLOGE("post_stream: HTTP %ld, response=%s", http_code, log::truncate(capture.error_body, 1500).c_str());
                }
            } else {
                ZLOGI("post_stream: HTTP %ld OK", http_code);
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return ok;
        }

        static bool post_sync(
            const std::string& url,
            const std::string& body,
            const std::string& api_key,
            std::string& response_out,
            std::string& error_out,
            bool verify_ssl = false
        ) {
            CURL* curl = curl_easy_init();
            if (!curl) {
                error_out = "Failed to init curl";
                ZLOGE("post_sync: Failed to init curl");
                return false;
            }

            ZLOGI("post_sync: URL=%s body=%s", url.c_str(), log::truncate(body, 1000).c_str());
            ZLOGI("post_sync: api_key=%s", log::mask_key(api_key).c_str());

            struct curl_slist* headers = nullptr;
            if (!api_key.empty()) {
                std::string auth_header = "Authorization: Bearer " + api_key;
                headers = curl_slist_append(headers, auth_header.c_str());
            }
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, "Accept: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sync_write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_out);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

            if (!verify_ssl) {
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            }

            // Enable abort check via progress callback
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, abort_progress_callback);
            curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, nullptr);

            CURLcode res = curl_easy_perform(curl);

            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

            bool ok = (res == CURLE_OK && http_code < 400);

            if (!ok) {
                if (res != CURLE_OK) {
                    error_out = curl_easy_strerror(res);
                    ZLOGE("post_sync: CURL error=%s", error_out.c_str());
                } else {
                    error_out = "HTTP " + std::to_string(http_code) + ": " + response_out;
                    ZLOGE("post_sync: HTTP %ld, response=%s", http_code, log::truncate(response_out, 1500).c_str());
                }
            } else {
                ZLOGI("post_sync: HTTP %ld OK, response=%s", http_code, log::truncate(response_out, 500).c_str());
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return ok;
        }

    private:
        static inline std::atomic<bool> g_abort_flag{false};

        struct StreamCapture {
            StreamCallback* user_cb = nullptr;
            std::string error_body;
            long http_code = 0;
        };

        static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
            auto* cap = static_cast<StreamCapture*>(userdata);
            size_t total = size * nitems;
            std::string header(buffer, total);
            if (header.find("HTTP/") == 0) {
                auto sp = header.find(' ');
                if (sp != std::string::npos) {
                    auto ep = header.find(' ', sp + 1);
                    std::string code_str = header.substr(sp + 1, ep != std::string::npos ? ep - sp - 1 : std::string::npos);
                    try { cap->http_code = std::stol(code_str); } catch (...) {}
                }
            }
            return total;
        }

        static size_t stream_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
            auto* cap = static_cast<StreamCapture*>(userdata);
            size_t total = size * nmemb;
            if (g_abort_flag.load()) {
                return 0;
            }
            if (cap->http_code >= 400) {
                cap->error_body.append(ptr, total);
            } else if (cap->user_cb) {
                (*cap->user_cb)(std::string(ptr, total));
            }
            return total;
        }

        static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
            auto* cb = static_cast<StreamCallback*>(userdata);
            size_t total = size * nmemb;
            (*cb)(std::string(ptr, total));
            return total;
        }

        static size_t sync_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
            auto* response = static_cast<std::string*>(userdata);
            response->append(ptr, size * nmemb);
            return size * nmemb;
        }

        static HttpResponse execute(const HttpRequest& req) {
            HttpResponse resp;

            CURL* curl = curl_easy_init();
            if (!curl) {
                resp.error = "Failed to init curl";
                return resp;
            }

            struct curl_slist* headers = nullptr;
            if (!req.api_key.empty()) {
                std::string auth_header = "Authorization: Bearer " + req.api_key;
                headers = curl_slist_append(headers, auth_header.c_str());
            }
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers,
                req.stream ? "Accept: text/event-stream" : "Accept: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());

            if (req.method == "POST") {
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
            }

            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sync_write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)req.timeout_seconds);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

            if (!req.verify_ssl) {
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            }

            if (!req.user_agent.empty()) {
                curl_easy_setopt(curl, CURLOPT_USERAGENT, req.user_agent.c_str());
            }

            CURLcode res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);

            if (res != CURLE_OK) {
                resp.error = curl_easy_strerror(res);
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return resp;
        }
    };
}
