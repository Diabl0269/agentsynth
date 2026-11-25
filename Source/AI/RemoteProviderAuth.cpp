// RemoteProvider: the account token, its lock and the refresh-on-401 hook (docs/ai/accounts.md#expiry-and-refresh).
#include "RemoteProvider.h"
#include "ServerUrlPolicy.h"

namespace synth {

void RemoteProvider::setAuthToken(const juce::String& token) {
    const juce::ScopedLock sl(authLock);
    authToken = token;
}

void RemoteProvider::setAuthRefresher(AuthRefresher refresher) {
    const juce::ScopedLock sl(authLock);
    authRefresher = std::move(refresher);
}

juce::String RemoteProvider::currentAuthToken() const {
    const juce::ScopedLock sl(authLock);
    return authToken;
}

bool RemoteProvider::refreshAuthorization(juce::StringPairArray& headers) {
    AuthRefresher refresher;
    juce::String rejected;
    {
        const juce::ScopedLock sl(authLock);
        refresher = authRefresher;
        rejected = authToken;
    }
    if (!serverurl::isOfficialServer(remoteHost) || !refresher || rejected.isEmpty())
        return false;

    const juce::String fresh = refresher(rejected); // blocks one round trip, never holds authLock
    if (fresh.isEmpty() || fresh == rejected)
        return false;

    {
        const juce::ScopedLock sl(authLock);
        authToken = fresh;
    }
    headers.set("Authorization", "Bearer " + fresh);
    return true;
}

} // namespace synth
