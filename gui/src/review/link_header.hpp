#pragma once

// link_header — a tiny RFC-5988 `Link:` header parser for pagination.
//
// GitHub (and other providers) page their list endpoints with a `Link:` response
// header of the form:
//
//   Link: <https://api.github.com/…?page=2>; rel="next",
//         <https://api.github.com/…?page=5>; rel="last"
//
// Following pagination just means "keep fetching rel=\"next\" until it's gone".
// This helper extracts that one URL. It is deliberately dependency-free (pure
// string parsing over the HttpResponse header value) so it can be unit-tested in
// isolation and reused by any provider; it does NOT depend on HttpClient.

#include <string>
#include <string_view>

namespace diffy::review {

// Return the URL whose parameters contain rel="next" (case-insensitive on the
// `rel` param name and the `next` token), or "" if there is no such link.
// Accepts the raw value of a single `Link` header (comma-separated link-values,
// each `<uri>; param=value; …`). Extra whitespace and other rel types are
// tolerated and ignored.
std::string next_link(std::string_view link_header);

}  // namespace diffy::review
