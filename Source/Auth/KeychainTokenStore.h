#pragma once

#include "InMemoryTokenStore.h"
#include "TokenStore.h"
#include <juce_core/juce_core.h>

namespace synth {

/**
 * @class KeychainTokenStore
 * @brief Persists the refresh token in the platform credential store under a fixed
 *        service/account pair — this is a single-account desktop app, so there is only ever one
 *        credential to store.
 *
 * One class, one header, works on every platform: on macOS (JUCE_MAC) the three methods talk to
 * the Keychain (Security.framework's SecItem* C API, kSecClassGenericPassword); on Windows
 * (JUCE_WINDOWS) to the Windows Credential Manager (a CRED_TYPE_GENERIC credential named after the
 * service string, persisted per machine and readable only by the signed-in user); on Linux they
 * delegate to an internal InMemoryTokenStore, so the user signs in again each launch there.
 * Callers never need to `#ifdef` around this type.
 */
class KeychainTokenStore : public TokenStore {
public:
    /** Production use: derives the Keychain service string from Branding.h's bundle id. */
    KeychainTokenStore();

    /** Test use: an explicit service string, so tests never share a service name with (and can
        never collide with) a real user's stored token. */
    explicit KeychainTokenStore(juce::String serviceName);

    /** Process-wide switch for the test binary: every store built afterwards with the default
        (production) constructor keeps its token in memory and never touches the platform
        credential store, so a MainComponent a test builds cannot read the developer's real
        sign-in item (which prompts for the login keychain password under an unsigned or
        re-signed build). Call once at startup, before any AccountService exists. */
    static void useInMemoryStoreForProcess(bool enable);

    /** True when this instance keeps its token in memory only (the switch above, or Linux). */
    bool isMemoryOnly() const;

    bool save(const juce::String& refreshToken) override;
    juce::String load() const override;
    void clear() override;

private:
    juce::String service;
    bool memoryOnly = false;
    InMemoryTokenStore memory;

#if JUCE_MAC
    // Read once per process (see load()); guarded because save() runs on the worker thread.
    mutable juce::CriticalSection lock;
    mutable juce::String cached;
    mutable bool cacheValid = false;
#endif
};

} // namespace synth
