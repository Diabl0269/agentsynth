#pragma once

#include "TokenStore.h"
#include <juce_core/juce_core.h>
#include <mutex>

namespace synth {

/**
 * @class InMemoryTokenStore
 * @brief Trivial process-lifetime TokenStore: no persistence at all. Used for tests and as the
 *        non-macOS fallback inside KeychainTokenStore. Thread-safe: the account service's worker
 *        thread clears or saves the token while the message thread reads it.
 */
class InMemoryTokenStore : public TokenStore {
public:
    bool save(const juce::String& refreshToken) override {
        const std::lock_guard<std::mutex> lock(mutex);
        token = refreshToken;
        return true;
    }

    juce::String load() const override {
        const std::lock_guard<std::mutex> lock(mutex);
        return token;
    }

    void clear() override {
        const std::lock_guard<std::mutex> lock(mutex);
        token.clear();
    }

private:
    mutable std::mutex mutex;
    juce::String token;
};

} // namespace synth
