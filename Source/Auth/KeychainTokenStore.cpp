#include "KeychainTokenStore.h"
#include "../Branding.h"

#if JUCE_MAC
#include <Security/Security.h>
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

bool KeychainTokenStore::save(const juce::String& refreshToken) {
    CFStringRef serviceRef = nullptr;
    CFStringRef accountRef = nullptr;
    CFMutableDictionaryRef query = makeBaseQuery(service, serviceRef, accountRef);

    // Simplest correct approach: delete whatever is there (ignoring the result — "nothing to
    // delete" is not an error here), then add fresh. Avoids SecItemUpdate's separate
    // query/attributes-to-update split for a single-item store where there's nothing to gain
    // from an in-place update.
    SecItemDelete(query);

    const auto utf8 = refreshToken.toRawUTF8();
    CFDataRef dataRef = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(utf8),
                                     static_cast<CFIndex>(refreshToken.getNumBytesAsUTF8()));
    CFDictionarySetValue(query, kSecValueData, dataRef);
    CFDictionarySetValue(query, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);

    const OSStatus status = SecItemAdd(query, nullptr);

    CFRelease(dataRef);
    CFRelease(query);
    CFRelease(serviceRef);
    CFRelease(accountRef);

    return status == errSecSuccess;
}

juce::String KeychainTokenStore::load() const {
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

    return value;
}

void KeychainTokenStore::clear() {
    CFStringRef serviceRef = nullptr;
    CFStringRef accountRef = nullptr;
    CFMutableDictionaryRef query = makeBaseQuery(service, serviceRef, accountRef);

    SecItemDelete(query); // no-op (ignored) if nothing was stored

    CFRelease(query);
    CFRelease(serviceRef);
    CFRelease(accountRef);
}

#else // !JUCE_MAC

// `service` is only ever read on JUCE_MAC (see makeBaseQuery() above); referencing it here keeps
// -Wunused-private-field quiet on every other platform without an #ifdef around the member itself.
bool KeychainTokenStore::save(const juce::String& refreshToken) {
    juce::ignoreUnused(service);
    return fallback.save(refreshToken);
}

juce::String KeychainTokenStore::load() const { return fallback.load(); }

void KeychainTokenStore::clear() { fallback.clear(); }

#endif

} // namespace synth
