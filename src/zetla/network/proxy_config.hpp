#pragma once
#include <string>
#include <mutex>

namespace zetla::network {

    struct ProxyConfig {
        bool enabled = false;
        std::string url;
        std::string secret;
    };

    // Thread-safe global proxy configuration. When active, LLM requests are
    // rewritten to route through the streaming forward proxy (X-Target-Host +
    // X-Proxy-Key headers). Web search and all other traffic is unaffected.
    class ProxyState {
    public:
        static void set(const std::string& url, const std::string& secret, bool enabled) {
            std::lock_guard<std::mutex> lk(mtx());
            auto& c = get();
            c.enabled = enabled;
            c.url = url;
            c.secret = secret;
        }

        static ProxyConfig get_config() {
            std::lock_guard<std::mutex> lk(mtx());
            return get();
        }

        static bool active() {
            std::lock_guard<std::mutex> lk(mtx());
            const auto& c = get();
            return c.enabled && !c.url.empty();
        }

    private:
        static std::mutex& mtx() { static std::mutex m; return m; }
        static ProxyConfig& get() { static ProxyConfig c; return c; }
    };

}
