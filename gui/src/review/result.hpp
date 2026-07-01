#pragma once

// diffy_review — error handling + pagination primitives.
//
// Every method on the review layer returns a Result<T>: EITHER a value OR a
// normalized Error. We do this instead of throwing so the UI thread (which
// runs these via the async load_threads path) can branch on a small, fixed set
// of ErrorKinds without wrapping every call site in try/catch — an auth failure,
// a rate-limit, or a "provider can't do that" all become data the composer /
// banners render uniformly, regardless of which backend produced them. Providers
// translate their HTTP status codes and JSON error bodies into this shape; the
// UI never sees a provider's raw error. This header is self-contained on purpose
// (only standard headers) so it can be included from model-free code and tests.

#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace diffy::review {

// The normalized failure taxonomy. Providers map their transport- and API-level
// errors onto exactly one of these so the UI can react without knowing the
// backend: Auth -> re-show the Connect card; RateLimited -> back off (see
// Error::retry_after_secs); NotFound -> the PR/file is gone; Unsupported -> the
// capability flag is off (a gated method was called anyway); Network -> offline
// banner, keep last-good cache; Parse -> the response didn't match what we
// expected; Other -> anything not worth a dedicated branch.
enum class ErrorKind {
    Auth,
    RateLimited,
    NotFound,
    Unsupported,
    Network,
    Parse,
    Other,
};

// A normalized error. `http_status` is the originating HTTP status when there was
// one (0 otherwise); `message` is a human-readable detail safe to surface;
// `retry_after_secs` is populated for RateLimited when the server told us how
// long to wait (from a Retry-After header or equivalent).
struct Error {
    ErrorKind kind = ErrorKind::Other;
    int http_status = 0;
    std::string message;
    std::optional<int> retry_after_secs;
};

// Monostate placeholder so Result<void> works via the generic template below:
// a "success with no payload" (approve/unapprove/etc.) carries a Monostate value.
// Prefer writing `Result<void>` at call sites — the alias below maps it here.
struct Monostate {};

namespace detail {
// Map void -> Monostate so Result<void> stores a real (empty) value type. All
// other T pass through unchanged.
template <class T>
struct ResultStorage {
    using type = T;
};
template <>
struct ResultStorage<void> {
    using type = Monostate;
};
}  // namespace detail

// Result<T> holds EITHER a T (success) OR an Error (failure), never both and never
// neither. Construct via the ok()/err() factories. `Result<void>` is valid and
// stores an empty Monostate — use ok() with no argument for it. Access value()
// only after checking has_value()/operator bool; access error() only when it is
// false. (Both check with an assert in debug builds; misuse is a programming bug,
// not a runtime error to recover from.)
template <class T>
class Result {
   public:
    using value_type = typename detail::ResultStorage<T>::type;

    // --- factories ---------------------------------------------------------

    static Result ok(value_type v) {
        Result r;
        r.data_ = std::move(v);
        return r;
    }

    // ok() with no argument: only enabled for Result<void> (value_type==Monostate).
    template <class U = T, class = std::enable_if_t<std::is_void_v<U>>>
    static Result ok() {
        Result r;
        r.data_ = Monostate{};
        return r;
    }

    static Result err(Error e) {
        Result r;
        r.data_ = std::move(e);
        return r;
    }

    // --- queries -----------------------------------------------------------

    bool has_value() const { return std::holds_alternative<value_type>(data_); }
    explicit operator bool() const { return has_value(); }

    value_type& value() { return std::get<value_type>(data_); }
    const value_type& value() const { return std::get<value_type>(data_); }

    const Error& error() const { return std::get<Error>(data_); }

   private:
    Result() = default;
    std::variant<value_type, Error> data_;
};

// The neutral pagination shape the public interface returns. HTTP-level paging
// (page numbers vs. opaque cursors vs. RFC5988 Link headers) is an internal
// detail of each provider; callers only ever see `items`, whether there is
// `has_more`, and the opaque `next_cursor` to pass back in for the next page.
template <class T>
struct Page {
    std::vector<T> items;
    bool has_more = false;
    std::string next_cursor;
};

}  // namespace diffy::review
