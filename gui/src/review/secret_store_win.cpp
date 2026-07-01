// secret_store_win.cpp — Windows Credential Manager backend for SecretStore.
//
// WHY: on Windows the natural, per-user encrypted credential vault is the
// Credential Manager, reached through the wincred.h API (implemented in
// Advapi32.dll). We store each secret as a CRED_TYPE_GENERIC credential whose
// TargetName is the caller's `key` and whose CredentialBlob is the raw UTF-8 bytes
// of the secret. Persistence is CRED_PERSIST_LOCAL_MACHINE so the credential
// survives logoff and is available to the user across sessions on this machine
// (as opposed to CRED_PERSIST_SESSION, which would vanish at logoff, or
// CRED_PERSIST_ENTERPRISE, which roams with the user's domain profile — we
// deliberately keep tokens local to this machine rather than roaming them).
//
// The whole file is guarded so it is a no-op TU on non-Windows platforms.

#if defined(_WIN32)

#include "secret_store.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>

#include <cstring>
#include <string>
#include <vector>

namespace diffy::review {

namespace {

// UTF-8 (std::string) -> UTF-16 (std::wstring). Mirrors main.cpp's to_wide().
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

// Overwrite a buffer's bytes before releasing it, so a plaintext secret does not
// linger in freed memory. SecureZeroMemory is not elided by the optimizer.
void
scrub(void* p, size_t n) {
    if (p && n) {
        SecureZeroMemory(p, n);
    }
}

}  // namespace

bool
SecretStore::set(const std::string& key, const std::string& secret) {
    const std::wstring target = to_wide(key);

    // Copy the UTF-8 secret bytes into a mutable buffer we control, so we can scrub
    // it after the CredWriteW call regardless of how std::string manages storage.
    std::vector<BYTE> blob(secret.begin(), secret.end());

    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.CredentialBlob = blob.empty() ? nullptr : blob.data();
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

    const BOOL ok = CredWriteW(&cred, 0);

    scrub(blob.data(), blob.size());
    return ok == TRUE;
}

std::optional<std::string>
SecretStore::get(const std::string& key) {
    const std::wstring target = to_wide(key);

    PCREDENTIALW cred = nullptr;
    if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) != TRUE) {
        // Includes ERROR_NOT_FOUND — treat any read failure as "no secret".
        return std::nullopt;
    }

    std::string secret(reinterpret_cast<const char*>(cred->CredentialBlob),
                       static_cast<size_t>(cred->CredentialBlobSize));

    // Scrub the OS-owned copy of the blob before handing it back to Credential
    // Manager's allocator, then free it.
    scrub(cred->CredentialBlob, cred->CredentialBlobSize);
    CredFree(cred);

    return secret;
}

bool
SecretStore::erase(const std::string& key) {
    const std::wstring target = to_wide(key);
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == TRUE;
}

}  // namespace diffy::review

#endif  // defined(_WIN32)
