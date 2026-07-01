#pragma once

// secret_store — a cross-platform secret store backed by the OS credential vault.
//
// WHY: auth tokens (PATs, OAuth access/refresh tokens) must never be persisted in
// diffy's plaintext config files. Instead we hand them to the operating system's
// credential vault — Windows Credential Manager, macOS Keychain — which encrypts
// them at rest and scopes them to the current user/machine. Config only ever
// stores the *shape* of a credential (provider id, base url, account); the secret
// itself is fetched on demand via SecretStore::get and is never written to disk by
// us. See REVIEW-ROADMAP.md §7 (SecretStore) and §9 (per-OS backends).
//
// This slice is intentionally self-contained: it depends on no other review header,
// only standard headers plus the OS credential APIs, so it can be linked and unit-
// tested in isolation. The API is minimal and synchronous by design.
//
// Secrets returned or accepted here are SENSITIVE: callers must never log them,
// include them in error messages, or otherwise surface them. Treat the returned
// std::string like a password — scrub it when you are done if you can.

#include <optional>
#include <string>

namespace diffy::review {

// Persists auth secrets in the OS credential vault (Windows Credential Manager /
// macOS Keychain), never in plaintext config. `key` is an opaque, stable
// identifier the caller composes, e.g. build_key(provider_id, base_url, account).
//
// `key` is used verbatim as the vault entry's identity: on Windows it is the
// generic credential `TargetName`; on macOS it is the `kSecAttrAccount` attribute
// (under a fixed service name). It must therefore be stable across runs — the same
// logical credential must always produce the same key — and callers should treat
// it as opaque. Compose it with build_key() rather than hand-rolling the scheme.
//
// All operations are synchronous and best-effort: set/erase return false on
// failure, get returns std::nullopt when the entry is absent (or on error). The
// stored secret is opaque bytes (UTF-8 text in practice); it is never logged.
struct SecretStore {
    // Create or overwrite the vault entry named `key` with `secret`. Returns true
    // on success. `secret` is treated as sensitive and must not be logged.
    static bool set(const std::string& key, const std::string& secret);

    // Fetch the secret stored under `key`, or std::nullopt if none exists (or on
    // error). The returned string is sensitive; scrub it when done if possible.
    static std::optional<std::string> get(const std::string& key);

    // Remove the vault entry named `key`. Returns true if it was removed (or was
    // already absent, depending on backend semantics — see the .cpp/.mm files).
    static bool erase(const std::string& key);
};

// Helper to compose a stable key; keep the scheme in one place. The scheme is an
// implementation detail (see secret_store.cc) but is guaranteed stable so that a
// credential written by one build is readable by the next.
std::string build_key(const std::string& provider_id, const std::string& base_url,
                      const std::string& account);

}  // namespace diffy::review
