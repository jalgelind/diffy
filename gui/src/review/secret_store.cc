// secret_store.cc — platform-neutral bits of the secret store.
//
// This translation unit compiles on every platform and holds the pieces that do
// not touch an OS credential API. Currently that is just build_key(), which owns
// the credential-naming scheme. The per-OS backends (set/get/erase) live in
// secret_store_win.cpp and secret_store_mac.mm, each guarded so only the matching
// platform compiles it. See secret_store.hpp for the WHY.

#include "secret_store.hpp"

namespace diffy::review {

// The key scheme. It is deliberately human-readable and prefixed with a fixed
// namespace so diffy's credentials are easy to spot in the OS vault UI and cannot
// collide with unrelated entries.
//
// STABILITY CONTRACT: this format is used verbatim as the vault entry identity, so
// it must never change for a given logical credential — altering it would orphan
// every previously stored secret (users would appear "logged out"). If the scheme
// ever must change, do so with an explicit migration, not silently.
std::string
build_key(const std::string& provider_id, const std::string& base_url,
          const std::string& account) {
    return "diffy-review/" + provider_id + "/" + base_url + "/" + account;
}

}  // namespace diffy::review
