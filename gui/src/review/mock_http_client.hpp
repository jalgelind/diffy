#pragma once

// MockHttpClient — a header-only, dependency-free HttpClient for tests.
//
// The review layer's conformance battery (see conformance.hpp) must run without
// touching the network — CI has no credentials and no route to the API hosts.
// Concrete providers (#27 Bitbucket, #28 GitHub) are therefore exercised against
// this mock: recorded JSON is registered per (method, url-substring), send()
// replays the first match, and every outbound request is recorded so a test can
// assert exactly what the provider called (verb, URL, headers, body).
//
// It is intentionally coupled to nothing but http_client.hpp + std: a test wires
// it, hands it to a provider, and reads back `sent`. A request that matches no
// registered response comes back as HttpError::Internal with a loud message so a
// missing fixture fails the test rather than silently returning empty data.

#include "http_client.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace diffy::review {

class MockHttpClient : public HttpClient {
  public:
    // Register a canned response: send() replays `resp` for the first request
    // whose method equals `method` (case-insensitive) and whose URL contains
    // `url_substring`. Registrations are matched in insertion order.
    void
    on(const std::string& method, const std::string& url_substring, HttpResponse resp) {
        canned_.push_back(Rule{method, url_substring, HttpError::None, std::string{}, std::move(resp)});
    }

    // GET-defaulting overload.
    void
    on(const std::string& url_substring, HttpResponse resp) {
        on("GET", url_substring, std::move(resp));
    }

    // Queue a normalized network error (or any HttpError) for matching requests,
    // so tests can exercise a provider's error-normalization paths.
    void
    on_error(const std::string& method,
             const std::string& url_substring,
             HttpError error,
             std::string message = "mock network error") {
        canned_.push_back(Rule{method, url_substring, error, std::move(message), HttpResponse{}});
    }

    void
    on_error(const std::string& url_substring, HttpError error, std::string message = "mock network error") {
        on_error("GET", url_substring, error, std::move(message));
    }

    // Replay the first registered rule whose method + url-substring match; if
    // none match, return a loud Internal error so the test fails visibly.
    HttpResult
    send(const HttpRequest& req) override {
        sent.push_back(req);
        for (const auto& rule : canned_) {
            if (matches(rule, req)) {
                HttpResult r;
                r.error = rule.error;
                r.message = rule.message;
                r.response = rule.response;
                return r;
            }
        }
        HttpResult miss;
        miss.error = HttpError::Internal;
        miss.message = "MockHttpClient: no registered response for " + req.method + " " + req.url;
        return miss;
    }

    // True if any recorded request's URL contains `url_substring`.
    bool
    requested(std::string_view url_substring) const {
        for (const auto& req : sent) {
            if (contains(req.url, url_substring)) {
                return true;
            }
        }
        return false;
    }

    // Every request passed to send(), in call order. Public so tests assert on
    // verb / URL / headers / body directly.
    std::vector<HttpRequest> sent;

  private:
    struct Rule {
        std::string method;
        std::string url_substring;
        HttpError error = HttpError::None;
        std::string message;
        HttpResponse response;
    };

    static bool
    contains(std::string_view haystack, std::string_view needle) {
        return needle.empty() || haystack.find(needle) != std::string_view::npos;
    }

    static bool
    iequals(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            unsigned char ca = static_cast<unsigned char>(a[i]);
            unsigned char cb = static_cast<unsigned char>(b[i]);
            if (lower(ca) != lower(cb)) {
                return false;
            }
        }
        return true;
    }

    static unsigned char
    lower(unsigned char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
    }

    static bool
    matches(const Rule& rule, const HttpRequest& req) {
        return iequals(rule.method, req.method) && contains(req.url, rule.url_substring);
    }

    std::vector<Rule> canned_;
};

}  // namespace diffy::review
