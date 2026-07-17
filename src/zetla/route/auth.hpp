#pragma once
#include "../core/types.hpp"
#include "../core/error.hpp"
#include <string>
#include <functional>
#include <memory>

namespace zetla::route {

    struct AuthInput {
        std::string method;
        std::string url;
        std::string body;
    };

    struct AuthHeaders {
        std::string authorization;
        std::unordered_map<std::string, std::string> extra;
    };

    struct AuthResult {
        AuthHeaders headers;
        std::string error;
        bool has_error = false;
    };

    using AuthApply = std::function<AuthResult(const AuthInput&)>;

    class Auth {
    public:
        Auth() = default;
        explicit Auth(AuthApply apply) : apply_(std::move(apply)) {}

        AuthResult operator()(const AuthInput& input) const {
            if (apply_) return apply_(input);
            AuthResult r;
            r.has_error = false;
            return r;
        }

        bool has_auth() const { return apply_ != nullptr; }

        static Auth none() { return Auth(); }

        static Auth bearer(const std::string& api_key) {
            return Auth([api_key](const AuthInput&) -> AuthResult {
                AuthHeaders h;
                h.authorization = "Bearer " + api_key;
                AuthResult r;
                r.headers = h;
                r.has_error = false;
                return r;
            });
        }

        static Auth header(const std::string& name, const std::string& value) {
            return Auth([name, value](const AuthInput&) -> AuthResult {
                AuthHeaders h;
                h.extra[name] = value;
                AuthResult r;
                r.headers = h;
                r.has_error = false;
                return r;
            });
        }

        Auth or_else(Auth fallback) const {
            if (apply_) return *this;
            return fallback;
        }

    private:
        AuthApply apply_;
    };
}
