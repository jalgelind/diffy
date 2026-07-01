// secret_store_mac.mm — macOS Keychain backend for SecretStore (Objective-C++).
//
// WHY: on macOS the per-user encrypted credential vault is the Keychain, reached
// through Keychain Services in Security.framework. We store each secret as a
// generic-password item (kSecClassGenericPassword) under a fixed service name
// (kSecAttrService = "diffy-review") with the caller's `key` as the account
// (kSecAttrAccount). Service+account together identify the item, so the same key
// always maps to the same Keychain entry.
//
// This file is Objective-C++ (.mm) because CoreFoundation/Security types interop
// most cleanly there, and it links Security.framework + CoreFoundation. It is
// guarded for __APPLE__ and will not be compiled on the Windows CI box, so it is
// written carefully for later macOS compilation rather than iterated on here.

#if defined(__APPLE__)

#include "secret_store.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <string>

namespace diffy::review {

namespace {

// Fixed service name grouping all of diffy's review credentials in the Keychain.
CFStringRef service_name() {
    return CFSTR("diffy-review");
}

// std::string (UTF-8) -> CFStringRef. Caller releases. Returns nullptr on failure.
CFStringRef
make_cfstring(const std::string& s) {
    return CFStringCreateWithBytes(kCFAllocatorDefault,
                                   reinterpret_cast<const UInt8*>(s.data()),
                                   static_cast<CFIndex>(s.size()),
                                   kCFStringEncodingUTF8, false);
}

// Build the base query dict that identifies one credential (class/service/account).
// Caller owns the returned dict and must CFRelease it. Returns nullptr on failure.
CFMutableDictionaryRef
make_base_query(const std::string& key) {
    CFStringRef account = make_cfstring(key);
    if (!account) {
        return nullptr;
    }
    CFMutableDictionaryRef query =
        CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks);
    if (query) {
        CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(query, kSecAttrService, service_name());
        CFDictionarySetValue(query, kSecAttrAccount, account);
    }
    CFRelease(account);  // retained by the dict
    return query;
}

}  // namespace

bool
SecretStore::set(const std::string& key, const std::string& secret) {
    CFMutableDictionaryRef query = make_base_query(key);
    if (!query) {
        return false;
    }

    CFDataRef data = CFDataCreate(kCFAllocatorDefault,
                                  reinterpret_cast<const UInt8*>(secret.data()),
                                  static_cast<CFIndex>(secret.size()));
    if (!data) {
        CFRelease(query);
        return false;
    }

    // Try to update an existing item first: the update attributes carry only the
    // new data (the query already pins class/service/account).
    CFMutableDictionaryRef attrs =
        CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks);
    if (!attrs) {
        CFRelease(data);
        CFRelease(query);
        return false;
    }
    CFDictionarySetValue(attrs, kSecValueData, data);

    OSStatus status = SecItemUpdate(query, attrs);
    if (status == errSecItemNotFound) {
        // No existing item: add one. Fold the data into the query and insert.
        CFDictionarySetValue(query, kSecValueData, data);
        status = SecItemAdd(query, nullptr);
        if (status == errSecDuplicateItem) {
            // Raced with another writer that created it: fall back to update.
            status = SecItemUpdate(query, attrs);
        }
    }

    CFRelease(attrs);
    CFRelease(data);
    CFRelease(query);
    return status == errSecSuccess;
}

std::optional<std::string>
SecretStore::get(const std::string& key) {
    CFMutableDictionaryRef query = make_base_query(key);
    if (!query) {
        return std::nullopt;
    }
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);

    if (status != errSecSuccess || result == nullptr) {
        return std::nullopt;  // includes errSecItemNotFound
    }

    CFDataRef data = static_cast<CFDataRef>(result);
    std::string secret(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                       static_cast<size_t>(CFDataGetLength(data)));
    CFRelease(result);
    return secret;
}

bool
SecretStore::erase(const std::string& key) {
    CFMutableDictionaryRef query = make_base_query(key);
    if (!query) {
        return false;
    }
    const OSStatus status = SecItemDelete(query);
    CFRelease(query);
    // Absent item counts as success: erase() is idempotent from the caller's view.
    return status == errSecSuccess || status == errSecItemNotFound;
}

}  // namespace diffy::review

#endif  // defined(__APPLE__)
