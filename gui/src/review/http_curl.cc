// http_curl.cc — libcurl backend for HttpClient (Linux / fallback).
//
// On platforms without an OS-native HTTP+TLS stack we expose through the same
// interface, libcurl is the documented fallback (REVIEW-ROADMAP §9). libcurl is
// expected to be built against the platform's own TLS (OpenSSL/GnuTLS on Linux),
// so it still reuses the system trust store and proxy — no CA bundle of ours.
// The file is wrapped in `#if !defined(_WIN32) && !defined(__APPLE__)` so it
// compiles to nothing where a native backend exists; <curl/curl.h> and the
// libcurl link are therefore only pulled in on the platforms that use it.

#include "http_client.hpp"

#if !defined(_WIN32) && !defined(__APPLE__)

#include <curl/curl.h>

#include <cstddef>
#include <string>
#include <vector>

namespace diffy::review {
namespace {

// libcurl write callback: append received bytes to the target std::string.
std::size_t
write_body(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// libcurl header callback: called once per header line (including the status
// line and the trailing blank line). Split "Name: value\r\n" into a pair.
std::size_t
write_header(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* headers = static_cast<std::vector<std::pair<std::string, std::string>>*>(userdata);
    const std::size_t len = size * nmemb;
    std::string line(ptr, len);
    // Strip trailing CRLF.
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return len;  // status line ("HTTP/1.1 200 OK") or blank separator
    }
    std::string name = line.substr(0, colon);
    std::size_t vstart = colon + 1;
    while (vstart < line.size() && (line[vstart] == ' ' || line[vstart] == '\t')) {
        ++vstart;
    }
    headers->emplace_back(std::move(name), line.substr(vstart));
    return len;
}

// Map a CURLcode to our normalized error kind.
HttpError
map_error(CURLcode code) {
    switch (code) {
        case CURLE_OPERATION_TIMEDOUT:
            return HttpError::Timeout;
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_COULDNT_CONNECT:
        case CURLE_RECV_ERROR:
        case CURLE_SEND_ERROR:
            return HttpError::Network;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
            return HttpError::Tls;
        case CURLE_URL_MALFORMAT:
        case CURLE_UNSUPPORTED_PROTOCOL:
            return HttpError::BadUrl;
        default:
            return HttpError::Network;
    }
}

class CurlClient final : public HttpClient {
  public:
    CurlClient() {
        // Idempotent process-wide init; libcurl refcounts these calls.
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~CurlClient() override {
        curl_global_cleanup();
    }

    HttpResult
    send(const HttpRequest& req) override {
        HttpResult result;

        CURL* curl = curl_easy_init();
        if (!curl) {
            result.error = HttpError::Internal;
            result.message = "curl_easy_init failed";
            return result;
        }

        HttpResponse& resp = result.response;

        curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        if (req.timeout_secs > 0) {
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(req.timeout_secs));
        }
        // Honour the environment/system proxy (curl reads http_proxy/https_proxy
        // by default; leaving CURLOPT_PROXY unset preserves that behaviour).

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_body);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &write_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);

        if (!req.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
        }

        struct curl_slist* header_list = nullptr;
        for (const auto& [name, value] : req.headers) {
            const std::string line = name + ": " + value;
            header_list = curl_slist_append(header_list, line.c_str());
        }
        if (header_list) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        }

        const CURLcode code = curl_easy_perform(curl);
        if (code != CURLE_OK) {
            result.error = map_error(code);
            result.message = curl_easy_strerror(code);
        } else {
            long status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            resp.status = status;
            result.error = HttpError::None;
        }

        if (header_list) {
            curl_slist_free_all(header_list);
        }
        curl_easy_cleanup(curl);
        return result;
    }
};

}  // namespace

std::unique_ptr<HttpClient>
make_http_client() {
    return std::make_unique<CurlClient>();
}

}  // namespace diffy::review

#endif  // !_WIN32 && !__APPLE__
