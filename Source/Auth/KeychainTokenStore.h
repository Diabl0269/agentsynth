#pragma once

#include "InMemoryTokenStore.h"
#include "TokenStore.h"
#include <juce_core/juce_core.h>

namespace synth {

/**
 * @class KeychainTokenStore
 * @brief Persists the refresh token in the macOS Keychain (Security.framework's SecItem* C API,
 *        kSecClassGenericPassword) under a fixed service/account pair — this is a single-account
 *        desktop app, so there is only ever one credential to store.
 *
 * One class, one header, works on every platform: on macOS (JUCE_MAC) the three methods talk to
 * the real Keychain; everywhere else they delegate to an internal InMemoryTokenStore, so callers
 * never need to `#ifdef` around this type.
 */
class KeychainTokenStore : public TokenStore {
public:
    /** Production use: derives the Keychain service string from Branding.h's bundle id. */
    KeychainTokenStore();

    /** Test use: an explicit service string, so tests never share a service name with (and can
        never collide with) a real user's stored token. */
    explicit KeychainTokenStore(juce::String serviceName);

    bool save(const juce::String& refreshToken) override;
    juce::String load() const override;
    void clear() override;

private:
    juce::String service;

#if !JUCE_MAC
    InMemoryTokenStore fallback;
#endif
};

} // namespace synth
