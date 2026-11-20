#include "ServerUrlPolicy.h"
#include "Branding.h"

namespace synth::serverurl {

namespace {
constexpr const char* kSiteDomain = "agentsynth.app";

juce::String schemeOf(const juce::String& url) {
    const int colon = url.indexOf("://");
    return colon > 0 ? url.substring(0, colon).toLowerCase() : juce::String();
}
} // namespace

bool isLoopbackHost(const juce::String& host) {
    const auto h = host.trim().toLowerCase();
    return h == "localhost" || h == "127.0.0.1" || h == "::1" || h == "[::1]";
}

juce::String hostOf(const juce::String& url) {
    const auto trimmed = url.trim();
    const auto scheme = schemeOf(trimmed);
    if (scheme != "http" && scheme != "https")
        return {};
    auto rest = trimmed.substring(scheme.length() + 3);
    const int end = rest.indexOfAnyOf("/?#");
    if (end >= 0)
        rest = rest.substring(0, end);
    // Credentials in the address ("https://user@evil") make the visible host misleading; refuse them.
    if (rest.containsChar('@'))
        return {};
    if (rest.startsWithChar('[')) {
        const int close = rest.indexOfChar(']');
        return close > 0 ? rest.substring(1, close).toLowerCase() : juce::String();
    }
    const int colon = rest.indexOfChar(':');
    if (colon >= 0)
        rest = rest.substring(0, colon);
    return rest.toLowerCase();
}

bool isAcceptableServerUrl(const juce::String& url) {
    const auto host = hostOf(url);
    if (host.isEmpty())
        return false;
    const auto scheme = schemeOf(url.trim());
    return scheme == "https" || (scheme == "http" && isLoopbackHost(host));
}

bool isOfficialServer(const juce::String& url) {
    const auto host = hostOf(url);
    if (host.isEmpty() || !isAcceptableServerUrl(url))
        return false;
    if (host == hostOf(synth::branding::kApiBaseUrl))
        return true;
#ifndef NDEBUG
    return host == hostOf(synth::branding::resolveApiBaseUrl());
#else
    return false;
#endif
}

bool isTrustedVerificationUri(const juce::String& url) {
    const auto trimmed = url.trim();
    if (schemeOf(trimmed) != "https")
        return false;
    const auto host = hostOf(trimmed);
    if (host.isEmpty())
        return false;
    return host == hostOf(synth::branding::kApiBaseUrl) || host == kSiteDomain ||
           host.endsWith(juce::String(".") + kSiteDomain);
}

} // namespace synth::serverurl
