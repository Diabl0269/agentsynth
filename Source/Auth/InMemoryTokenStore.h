#pragma once

#include "TokenStore.h"
#include <juce_core/juce_core.h>

namespace synth {

/**
 * @class InMemoryTokenStore
 * @brief Trivial process-lifetime TokenStore: no persistence at all. Used for tests and as the
 *        non-macOS fallback inside KeychainTokenStore.
 */
class InMemoryTokenStore : public TokenStore {
public:
    bool save(const juce::String& refreshToken) override {
        token = refreshToken;
        return true;
    }

    juce::String load() const override { return token; }

    void clear() override { token.clear(); }

private:
    juce::String token;
};

} // namespace synth
