#pragma once
#include <string>
#include <functional>
#include <vector>

namespace zetla::network {

    struct SSEEvent {
        std::string data;
        std::string event;
        std::string id;
        int retry = -1;
        bool is_done = false;
    };

    using SSEFrameCallback = std::function<void(const SSEEvent&)>;

    class SSEParser {
    public:
        void feed(const std::string& chunk) {
            buffer_ += chunk;
            parse();
        }

        void set_callback(SSEFrameCallback cb) {
            callback_ = std::move(cb);
        }

        void reset() {
            buffer_.clear();
        }

    private:
        std::string buffer_;
        SSEFrameCallback callback_;

        void parse() {
            size_t pos = 0;
            while (pos < buffer_.size()) {
                size_t line_end = buffer_.find('\n', pos);
                if (line_end == std::string::npos) break;

                std::string_view line(buffer_.data() + pos, line_end - pos);
                pos = line_end + 1;

                while (!line.empty() && line.back() == '\r') {
                    line.remove_suffix(1);
                }

                if (line.empty()) {
                    continue;
                }

                if (line[0] == ':') continue;

                SSEEvent event;

                if (line.substr(0, 6) == "data: ") {
                    event.data = std::string(line.substr(6));
                } else if (line.substr(0, 7) == "event: ") {
                    event.event = std::string(line.substr(7));
                } else if (line.substr(0, 4) == "id: ") {
                    event.id = std::string(line.substr(4));
                } else if (line.substr(0, 7) == "retry: ") {
                    try { event.retry = std::stoi(std::string(line.substr(7))); } catch (...) {}
                } else {
                    continue;
                }

                if (!event.data.empty()) {
                    if (event.data == "[DONE]") {
                        event.is_done = true;
                    }
                    if (callback_) callback_(event);
                }
            }
            buffer_.erase(0, pos);
        }
    };
}
