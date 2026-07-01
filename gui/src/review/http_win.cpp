// http_win.cpp — WinHTTP backend for HttpClient (Windows only).
//
// WinHTTP is the OS-native HTTP stack: it reuses Schannel for TLS (the system
// trust store) and the machine/user proxy configuration, so we get correct
// certificate validation and corporate-proxy support with no bundled OpenSSL or
// CA bundle (see REVIEW-ROADMAP §9). This file is entirely wrapped in
// `#if defined(_WIN32)` so it compiles to nothing on other platforms, mirroring
// the existing per-OS split in main.cpp / gui/CMakeLists.txt.

#include "http_client.hpp"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

namespace diffy::review {
namespace {

// UTF-8 -> UTF-16, matching the to_wide() convention already used in main.cpp.
std::wstring
to_wide(const std::string& s) {
    if (s.empty()) {
        return std::wstring();
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    }
    return w;
}

// UTF-16 -> UTF-8 for header names/values coming back from WinHTTP.
std::string
to_utf8(const wchar_t* w, int wlen) {
    if (!w || wlen <= 0) {
        return std::string();
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, w, wlen, s.data(), n, nullptr, nullptr);
    }
    return s;
}

// Map a WinHTTP GetLastError() code to our normalized error kind.
HttpError
map_error(DWORD err) {
    switch (err) {
        case ERROR_WINHTTP_TIMEOUT:
            return HttpError::Timeout;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR:
            return HttpError::Network;
        case ERROR_WINHTTP_SECURE_FAILURE:
            return HttpError::Tls;
        case ERROR_WINHTTP_INVALID_URL:
        case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
            return HttpError::BadUrl;
        default:
            return HttpError::Internal;
    }
}

HttpResult
fail(HttpError kind, const std::string& what, DWORD err) {
    HttpResult r;
    r.error = kind;
    r.message = what + " (WinHTTP error " + std::to_string(err) + ")";
    return r;
}

// Split the WINHTTP_QUERY_RAW_HEADERS_CRLF blob ("Name: value\r\n...\r\n\r\n")
// into name/value pairs. The first line is the HTTP status line and is skipped.
void
parse_raw_headers(const std::wstring& raw, std::vector<std::pair<std::string, std::string>>& out) {
    std::size_t pos = 0;
    bool first = true;
    while (pos < raw.size()) {
        std::size_t eol = raw.find(L"\r\n", pos);
        std::wstring line = raw.substr(pos, eol == std::wstring::npos ? std::wstring::npos : eol - pos);
        pos = (eol == std::wstring::npos) ? raw.size() : eol + 2;
        if (line.empty()) {
            continue;  // trailing blank line
        }
        if (first) {
            first = false;  // status line, e.g. "HTTP/1.1 200 OK"
            continue;
        }
        std::size_t colon = line.find(L':');
        if (colon == std::wstring::npos) {
            continue;
        }
        std::wstring name = line.substr(0, colon);
        std::size_t vstart = colon + 1;
        while (vstart < line.size() && (line[vstart] == L' ' || line[vstart] == L'\t')) {
            ++vstart;
        }
        std::wstring value = line.substr(vstart);
        out.emplace_back(to_utf8(name.c_str(), static_cast<int>(name.size())),
                         to_utf8(value.c_str(), static_cast<int>(value.size())));
    }
}

// RAII wrappers so every early return closes its handles.
struct Internet {
    HINTERNET h = nullptr;
    ~Internet() {
        if (h) {
            WinHttpCloseHandle(h);
        }
    }
};

class WinHttpClient final : public HttpClient {
  public:
    HttpResult
    send(const HttpRequest& req) override {
        // Break the URL into components with WinHttpCrackUrl.
        std::wstring wurl = to_wide(req.url);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256] = {0};
        wchar_t path[4096] = {0};
        uc.lpszHostName = host;
        uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
            return fail(HttpError::BadUrl, "WinHttpCrackUrl failed", GetLastError());
        }
        const bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        const INTERNET_PORT port = uc.nPort;

        // Session: automatic proxy detection from the system configuration.
        Internet session;
        session.h = WinHttpOpen(L"diffy-review/1.0",
                                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS,
                                0);
        if (!session.h) {
            // Older Windows may not support AUTOMATIC_PROXY; fall back to default.
            session.h = WinHttpOpen(L"diffy-review/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
        }
        if (!session.h) {
            return fail(HttpError::Internal, "WinHttpOpen failed", GetLastError());
        }

        if (req.timeout_secs > 0) {
            const int ms = req.timeout_secs * 1000;
            WinHttpSetTimeouts(session.h, ms, ms, ms, ms);
        }

        Internet connect;
        connect.h = WinHttpConnect(session.h, host, port, 0);
        if (!connect.h) {
            return fail(HttpError::Network, "WinHttpConnect failed", GetLastError());
        }

        const DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
        Internet request;
        request.h = WinHttpOpenRequest(connect.h,
                                       to_wide(req.method).c_str(),
                                       path,
                                       nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       flags);
        if (!request.h) {
            return fail(HttpError::Internal, "WinHttpOpenRequest failed", GetLastError());
        }

        // Follow redirects (WinHTTP does this by default; make it explicit).
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

        // Add request headers (name: value, CRLF-separated, WINHTTP_ADDREQ_FLAG_ADD).
        std::wstring header_block;
        for (const auto& [name, value] : req.headers) {
            header_block += to_wide(name);
            header_block += L": ";
            header_block += to_wide(value);
            header_block += L"\r\n";
        }

        LPVOID body_ptr = WINHTTP_NO_REQUEST_DATA;
        DWORD body_len = 0;
        if (!req.body.empty()) {
            body_ptr = const_cast<char*>(req.body.data());
            body_len = static_cast<DWORD>(req.body.size());
        }

        if (!WinHttpSendRequest(request.h,
                                header_block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                                     : header_block.c_str(),
                                header_block.empty() ? 0 : static_cast<DWORD>(-1L),
                                body_ptr,
                                body_len,
                                body_len,
                                0)) {
            const DWORD e = GetLastError();
            return fail(map_error(e), "WinHttpSendRequest failed", e);
        }

        if (!WinHttpReceiveResponse(request.h, nullptr)) {
            const DWORD e = GetLastError();
            return fail(map_error(e), "WinHttpReceiveResponse failed", e);
        }

        HttpResult result;
        HttpResponse& resp = result.response;

        // Status code.
        DWORD status_code = 0;
        DWORD sc_size = sizeof(status_code);
        if (!WinHttpQueryHeaders(request.h,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX,
                                 &status_code,
                                 &sc_size,
                                 WINHTTP_NO_HEADER_INDEX)) {
            const DWORD e = GetLastError();
            return fail(HttpError::Internal, "WinHttpQueryHeaders(status) failed", e);
        }
        resp.status = static_cast<long>(status_code);

        // Raw response headers -> pairs.
        DWORD hdr_size = 0;
        WinHttpQueryHeaders(request.h,
                            WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            WINHTTP_NO_OUTPUT_BUFFER,
                            &hdr_size,
                            WINHTTP_NO_HEADER_INDEX);
        if (hdr_size > 0) {
            std::wstring raw(hdr_size / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(request.h,
                                    WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                    WINHTTP_HEADER_NAME_BY_INDEX,
                                    raw.data(),
                                    &hdr_size,
                                    WINHTTP_NO_HEADER_INDEX)) {
                // WinHTTP reports hdr_size in bytes including the terminating
                // NUL; resize to the actual character count it wrote.
                raw.resize(hdr_size / sizeof(wchar_t));
                if (!raw.empty() && raw.back() == L'\0') {
                    raw.pop_back();
                }
                parse_raw_headers(raw, resp.headers);
            }
        }

        // Body: loop WinHttpQueryDataAvailable / WinHttpReadData.
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request.h, &avail)) {
                const DWORD e = GetLastError();
                return fail(map_error(e), "WinHttpQueryDataAvailable failed", e);
            }
            if (avail == 0) {
                break;
            }
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request.h, chunk.data(), avail, &read)) {
                const DWORD e = GetLastError();
                return fail(map_error(e), "WinHttpReadData failed", e);
            }
            if (read == 0) {
                break;
            }
            resp.body.append(chunk.data(), read);
        }

        result.error = HttpError::None;
        return result;
    }
};

}  // namespace

std::unique_ptr<HttpClient>
make_http_client() {
    return std::make_unique<WinHttpClient>();
}

}  // namespace diffy::review

#endif  // _WIN32
