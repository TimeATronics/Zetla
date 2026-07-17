#pragma once
#include <string>
#include <optional>
#include <expected>

namespace zetla::core {

    enum class ErrorClass {
        InvalidRequest,
        Authentication,
        RateLimit,
        QuotaExceeded,
        ContentPolicy,
        ProviderInternal,
        Transport,
        InvalidProviderOutput,
        UnknownProvider,
        NoRoute
    };

    inline const char* error_class_to_string(ErrorClass c) {
        switch (c) {
            case ErrorClass::InvalidRequest: return "invalid_request";
            case ErrorClass::Authentication: return "authentication";
            case ErrorClass::RateLimit: return "rate_limit";
            case ErrorClass::QuotaExceeded: return "quota_exceeded";
            case ErrorClass::ContentPolicy: return "content_policy";
            case ErrorClass::ProviderInternal: return "provider_internal";
            case ErrorClass::Transport: return "transport";
            case ErrorClass::InvalidProviderOutput: return "invalid_provider_output";
            case ErrorClass::UnknownProvider: return "unknown_provider";
            case ErrorClass::NoRoute: return "no_route";
        }
        return "unknown";
    }

    struct LLMError {
        ErrorClass classification = ErrorClass::UnknownProvider;
        std::string message;
        std::string module;
        std::string method;
        bool retryable = false;
        std::optional<int> retry_after_ms;
        std::optional<int> http_status;

        std::string full_message() const {
            std::string result;
            if (!module.empty() && !method.empty()) {
                result = module + "." + method + ": ";
            }
            result += message;
            return result;
        }

        static LLMError authentication(const std::string& msg) {
            LLMError e;
            e.classification = ErrorClass::Authentication;
            e.message = msg;
            e.retryable = false;
            return e;
        }

        static LLMError rate_limit(const std::string& msg, std::optional<int> retry_after = std::nullopt) {
            LLMError e;
            e.classification = ErrorClass::RateLimit;
            e.message = msg;
            e.retryable = true;
            e.retry_after_ms = retry_after;
            return e;
        }

        static LLMError provider_internal(const std::string& msg, int http_status = 0, bool retryable = true) {
            LLMError e;
            e.classification = ErrorClass::ProviderInternal;
            e.message = msg;
            e.http_status = http_status;
            e.retryable = retryable;
            return e;
        }

        static LLMError transport(const std::string& msg) {
            LLMError e;
            e.classification = ErrorClass::Transport;
            e.message = msg;
            e.retryable = false;
            return e;
        }

        static LLMError invalid_request(const std::string& msg) {
            LLMError e;
            e.classification = ErrorClass::InvalidRequest;
            e.message = msg;
            e.retryable = false;
            return e;
        }

        static LLMError invalid_provider_output(const std::string& msg) {
            LLMError e;
            e.classification = ErrorClass::InvalidProviderOutput;
            e.message = msg;
            e.retryable = false;
            return e;
        }
    };

    inline LLMError classify_http_error(int status, const std::string& body = "") {
        switch (status) {
            case 401: return LLMError::authentication("Invalid API key");
            case 403: return LLMError::authentication("Insufficient permissions");
            case 429: return LLMError::rate_limit("Rate limited");
            case 500: case 502: case 503: case 504:
                return LLMError::provider_internal("Server error", status, true);
            default:
                if (status >= 400) {
                    return LLMError::invalid_request("HTTP " + std::to_string(status));
                }
                return LLMError::provider_internal("HTTP " + std::to_string(status), status, false);
        }
    }
}
