#pragma once

#include <juce_core/juce_core.h>

namespace synth {

/**
 * @class TokenStore
 * @brief Pure interface for persisting the single refresh token this desktop app ever holds.
 *
 * Never stores the access token — only the long-lived refresh token, which AccountService
 * exchanges for a fresh access token (in memory only) at startup and after sign-in.
 */
class TokenStore {
public:
    virtual ~TokenStore() = default;

    /** Persists `refreshToken`, replacing whatever was stored before. Returns false if the token
        could not be persisted (callers must not proceed as though it had been). */
    virtual bool save(const juce::String& refreshToken) = 0;

    /** Returns the stored refresh token, or an empty string if none is stored. */
    virtual juce::String load() const = 0;

    /** Removes any stored refresh token. A no-op if nothing was stored. */
    virtual void clear() = 0;
};

} // namespace synth
