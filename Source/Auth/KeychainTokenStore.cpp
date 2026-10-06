#include "KeychainTokenStore.h"
#include "../Branding.h"

#if JUCE_MAC
#include <Security/Security.h>
#elif JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <wincred.h>
#include <windows.h>
#pragma comment(lib, "advapi32.lib")
#endif

namespace synth {

namespace {
constexpr const char* kAccount = "default";
}

KeychainTokenStore::KeychainTokenStore()
    : service(juce::String(synth::branding::kBundleIdentifier) + ".refreshtoken") {}

KeychainTokenStore::KeychainTokenStore(juce::String serviceName)
    : service(std::move(serviceName)) {}

#if JUCE_MAC

namespace {

/** Builds the base query dictionary identifying "the one credential this app stores" — same
    service/account pair for save/load/clear, so callers only ever add the keys specific to their
    operation (kSecValueData for add, kSecReturnData for copy-matching, ...). Caller owns the
    returned dictionary and the two CFStringRefs it wraps; release all three. */
CFMutableDictionaryRef makeBaseQuery(const juce::String& service, CFStringRef& serviceRefOut,
                                     CFStringRef& accountRefOut) {
    serviceRefOut = service.toCFString();
    accountRefOut = juce::String(kAccount).toCFString();

    CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, serviceRefOut);
    CFDictionarySetValue(query, kSecAttrAccount, accountRefOut);
    return query;
}

} // namespace

namespace {

CFDataRef makeData(const juce::String& text) {
    return CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.toRawUTF8()),
                        static_cast<CFIndex>(text.getNumBytesAsUTF8()));
}

} // namespace

bool KeychainTokenStore::save(const juce::String& refreshToken) {
    const juce::ScopedLock sl(lock);

    // The same token again (a refresh that did not rotate it) needs no Keychain access at all.
    if (cacheValid && cached == refreshToken)
        return true;

    CFStringRef serviceRef = nullptr;
    CFStringRef accountRef = nullptr;
    CFMutableDictionaryRef query = makeBaseQuery(service, serviceRef, accountRef);
    CFDataRef dataRef = makeData(refreshToken);

    // Update in place first. Delete + add made a brand-new item on every save: macOS pins an
    // item to the code signature that created it, so once a rebuilt (or updated) app did that, it
    // asked for permission on the delete, and a refused delete left the old item behind so the add
    // failed as a duplicate and sign-in reported "could not persist the refresh token".
    CFMutableDictionaryRef changes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                               &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(changes, kSecValueData, dataRef);
    OSStatus status = SecItemUpdate(query, changes);
    CFRelease(changes);

    if (status != errSecSuccess) {
        // Nothing stored yet (first sign-in), or an item this build cannot update: replace it.
        // A delete the OS refuses is ignored; the add below then reports the real outcome.
        SecItemDelete(query);
        CFDictionarySetValue(query, kSecValueData, dataRef);
        CFDictionarySetValue(query, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);
        status = SecItemAdd(query, nullptr);
    }

    CFRelease(dataRef);
    CFRelease(query);
    CFRelease(serviceRef);
    CFRelease(accountRef);

    if (status != errSecSuccess)
        return false;

    cached = refreshToken;
    cacheValid = true;
    return true;
}

juce::String KeychainTokenStore::load() const {
    const juce::ScopedLock sl(lock);

    // Every Keychain read can raise a permission prompt when the running build is not the one that
    // created the item, so read once per process; this app is the only writer and keeps the cache
    // current in save()/clear().
    if (cacheValid)
        return cached;

    CFStringRef serviceRef = nullptr;
    CFStringRef accountRef = nullptr;
    CFMutableDictionaryRef query = makeBaseQuery(service, serviceRef, accountRef);

    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef resultRef = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &resultRef);

    juce::String value;
    if (status == errSecSuccess && resultRef != nullptr) {
        auto* dataRef = static_cast<CFDataRef>(resultRef);
        value = juce::String::fromUTF8(reinterpret_cast<const char*>(CFDataGetBytePtr(dataRef)),
                                       static_cast<int>(CFDataGetLength(dataRef)));
        CFRelease(resultRef);
    }

    CFRelease(query);
    CFRelease(serviceRef);
    CFRelease(accountRef);

    // A denied read (the user clicked Deny) is not cached: asking again later is legitimate.
    if (status == errSecSuccess || status == errSecItemNotFound) {
        cached = value;
        cacheValid = true;
    }
    return value;
}

void KeychainTokenStore::clear() {
    const juce::ScopedLock sl(lock);

    CFStringRef serviceRef = nullptr;
    CFStringRef accountRef = nullptr;
    CFMutableDictionaryRef query = makeBaseQuery(service, serviceRef, accountRef);

    const OSStatus status = SecItemDelete(query);

    if (status != errSecSuccess && status != errSecItemNotFound) {
        // The OS refused the delete (an item created by a differently signed build). Sign-out must
        // still take effect, so blank the stored value: load() treats an empty token as none.
        CFDataRef empty = makeData({});
        CFMutableDictionaryRef changes = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(changes, kSecValueData, empty);
        SecItemUpdate(query, changes);
        CFRelease(changes);
        CFRelease(empty);
    }

    CFRelease(query);
    CFRelease(serviceRef);
    CFRelease(accountRef);

    cached = juce::String();
    cacheValid = true;
}

#elif JUCE_WINDOWS

// Windows Credential Manager. The credential's target name is the service string, its user name the
// fixed account. A generic credential's blob is capped at CRED_MAX_CREDENTIAL_BLOB_SIZE (2560
// bytes), far above any refresh token this app stores; an oversized token makes save() return false.
bool KeychainTokenStore::save(const juce::String& refreshToken) {
    const auto target = service.toWideCharPointer();
    const auto account = juce::String(kAccount);

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target);
    credential.UserName = const_cast<LPWSTR>(account.toWideCharPointer());
    credential.CredentialBlobSize = static_cast<DWORD>(refreshToken.getNumBytesAsUTF8());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(refreshToken.toRawUTF8()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;

    return CredWriteW(&credential, 0) != FALSE;
}

juce::String KeychainTokenStore::load() const {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(service.toWideCharPointer(), CRED_TYPE_GENERIC, 0, &credential) || credential == nullptr)
        return {};

    const auto value = juce::String::fromUTF8(reinterpret_cast<const char*>(credential->CredentialBlob),
                                              static_cast<int>(credential->CredentialBlobSize));
    CredFree(credential);
    return value;
}

void KeychainTokenStore::clear() { CredDeleteW(service.toWideCharPointer(), CRED_TYPE_GENERIC, 0); }

#else // Linux and anything else

// `service` is only ever read on JUCE_MAC and JUCE_WINDOWS; referencing it here keeps
// -Wunused-private-field quiet on the remaining platform without an #ifdef around the member itself.
bool KeychainTokenStore::save(const juce::String& refreshToken) {
    juce::ignoreUnused(service);
    return fallback.save(refreshToken);
}

juce::String KeychainTokenStore::load() const { return fallback.load(); }

void KeychainTokenStore::clear() { fallback.clear(); }

#endif

} // namespace synth
