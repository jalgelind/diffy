#pragma once

// HttpClient — a tiny synchronous HTTP interface with OS-native backends.
//
// The review layer talks to a handful of HTTPS API hosts. The expensive
// cross-platform concern there is not the HTTP verbs but TLS trust + the system
// proxy, so — per REVIEW-ROADMAP §9 — we do NOT bundle a header-only client plus
// OpenSSL/mbedTLS and a CA-cert bundle. Instead each backend reuses the OS's own
// TLS stack, trust store and proxy: WinHTTP on Windows, NSURLSession on macOS,
// libcurl (built against Schannel/SecureTransport) as the Linux/fallback. This
// gives us zero third-party dependency and the smallest binary, all hidden
// behind the one interface below.
//
// send() is BLOCKING. It is always called from a worker thread (the existing
// load_threads path), NEVER the UI/event-loop thread. Backends honour the system
// proxy and follow redirects. This header is intentionally self-contained: it
// pulls in only standard library types so the backends can be unit-tested and
// wired independently of the neutral model / Result / capabilities headers.

#include <cctype>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace diffy::review {

// A single HTTP header. Backends also accept the plain pair form below; this
// named alias exists purely for readability at call sites that prefer it.
struct HttpHeader {
    std::string name;
    std::string value;
};

// An outbound request. `headers` are sent verbatim (order preserved). For a body
// the caller sets `body` and typically a Content-Type header. `timeout_secs`
// bounds the whole exchange; 0 means "backend default".
struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeout_secs = 30;
};

// The response as delivered by the OS stack after redirects have been followed.
// `status` is the final HTTP status line code (e.g. 200, 404); 0 means the
// exchange never produced a response (see HttpResult::error for why).
struct HttpResponse {
    long status = 0;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

// A normalized failure kind, so the review layer can react uniformly without
// knowing which backend produced it.
enum class HttpError {
    None,     // success — `response` is populated
    Network,  // DNS / connect / reset / no route
    Timeout,  // exceeded HttpRequest::timeout_secs
    Tls,      // handshake / certificate / trust failure
    BadUrl,   // malformed or unsupported URL
    Internal  // backend could not be initialized / unexpected API error
};

// The result of send(): either a response (error == None) or a normalized error
// with a human-readable message for logs. `response` may still carry a partial
// status on some error paths but callers should gate on ok() first.
struct HttpResult {
    HttpError error = HttpError::None;
    std::string message;
    HttpResponse response;

    bool
    ok() const {
        return error == HttpError::None;
    }
};

// The backend-agnostic client. One instance is cheap; callers may keep a single
// client and issue many send() calls (each blocking) from worker threads.
class HttpClient {
  public:
    virtual ~HttpClient() = default;

    // Perform the request and block until the OS stack delivers a response or
    // fails. Thread-safe with respect to distinct HttpClient instances; a single
    // instance is expected to be driven from one worker thread at a time.
    virtual HttpResult send(const HttpRequest&) = 0;

    // Case-insensitive lookup of a response header value; returns "" if absent.
    // HTTP header names are case-insensitive (RFC 7230 §3.2), and the different
    // OS stacks normalize casing differently, so callers must never compare
    // header names by exact case — use this helper instead. Defined inline
    // because it is backend-independent, while each backend .cc/.mm is
    // #if-guarded to a single platform (so a shared out-of-line definition would
    // vanish from builds whose backend file compiles to nothing).
    std::string
    header(const HttpResponse& resp, std::string_view name) const {
        const auto ieq = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i) {
                unsigned char ca = static_cast<unsigned char>(a[i]);
                unsigned char cb = static_cast<unsigned char>(b[i]);
                if (std::tolower(ca) != std::tolower(cb)) {
                    return false;
                }
            }
            return true;
        };
        for (const auto& [n, v] : resp.headers) {
            if (ieq(n, name)) {
                return v;
            }
        }
        return {};
    }
};

// Construct the platform-default backend: WinHTTP on Windows, NSURLSession on
// macOS, libcurl elsewhere. Never returns null on a supported platform.
std::unique_ptr<HttpClient> make_http_client();

}  // namespace diffy::review
