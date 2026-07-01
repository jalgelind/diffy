// link_header.cc — implementation of the RFC-5988 `Link:` next-link parser.
//
// The grammar we handle (a pragmatic subset of RFC 5988 / RFC 8288):
//
//   Link            = link-value *( "," link-value )
//   link-value      = "<" URI-Reference ">" *( ";" link-param )
//   link-param      = token "=" ( token | quoted-string )
//
// We are only after the URI whose params carry rel="next". The tricky part is
// that commas separate link-values but may also appear *inside* a quoted-string
// param value, and inside the angle-bracketed URI. We therefore split on commas
// that fall outside `<…>` and outside `"…"`, then inspect each piece.

#include "link_header.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace diffy::review {
namespace {

std::string
to_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string_view
trim(std::string_view s) {
    const auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && is_ws(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_ws(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

// Split the header on commas that are NOT inside <> or "".
std::vector<std::string_view>
split_link_values(std::string_view s) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    bool in_angle = false;
    bool in_quote = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (in_quote) {
            if (c == '"') {
                in_quote = false;
            }
            continue;
        }
        if (c == '"') {
            in_quote = true;
        } else if (c == '<') {
            in_angle = true;
        } else if (c == '>') {
            in_angle = false;
        } else if (c == ',' && !in_angle) {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.push_back(s.substr(start));
    return parts;
}

// Given one link-value ("<uri>; rel=\"next\"; …"), return the URI if it carries
// a rel param equal to "next" (a rel may hold space-separated types, e.g.
// rel="prev next" — we match if "next" appears among them). Otherwise "".
std::string
uri_if_next(std::string_view value) {
    value = trim(value);
    const std::size_t lt = value.find('<');
    if (lt == std::string_view::npos) {
        return {};
    }
    const std::size_t gt = value.find('>', lt + 1);
    if (gt == std::string_view::npos) {
        return {};
    }
    const std::string_view uri = value.substr(lt + 1, gt - lt - 1);

    // Scan the params after the closing '>'.
    std::string_view params = value.substr(gt + 1);
    bool is_next = false;
    while (!params.empty()) {
        const std::size_t semi = params.find(';');
        std::string_view param = trim(semi == std::string_view::npos ? params : params.substr(0, semi));
        params = (semi == std::string_view::npos) ? std::string_view{} : params.substr(semi + 1);
        if (param.empty()) {
            continue;
        }
        const std::size_t eq = param.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        std::string_view name = trim(param.substr(0, eq));
        std::string_view val = trim(param.substr(eq + 1));
        // Strip surrounding quotes from the value.
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        }
        if (to_lower(name) == "rel") {
            // rel value may be space-separated relation types.
            const std::string rel = to_lower(val);
            std::size_t p = 0;
            while (p < rel.size()) {
                std::size_t sp = rel.find(' ', p);
                std::string_view token(rel.data() + p, (sp == std::string::npos ? rel.size() : sp) - p);
                if (token == "next") {
                    is_next = true;
                    break;
                }
                if (sp == std::string::npos) {
                    break;
                }
                p = sp + 1;
            }
        }
    }
    return is_next ? std::string(uri) : std::string{};
}

}  // namespace

std::string
next_link(std::string_view link_header) {
    for (std::string_view value : split_link_values(link_header)) {
        std::string uri = uri_if_next(value);
        if (!uri.empty()) {
            return uri;
        }
    }
    return {};
}

}  // namespace diffy::review
