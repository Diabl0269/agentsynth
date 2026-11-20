#pragma once

#include <juce_core/juce_core.h>

namespace synth::serverurl {

/** True for localhost, 127.0.0.1 and ::1 (with or without brackets). Case-insensitive. */
bool isLoopbackHost(const juce::String& host);

/** The host part of an http(s) URL, lower-cased and without port or brackets; empty when the text is not an
    http(s) URL with a host. */
juce::String hostOf(const juce::String& url);

/** A server address the app may talk to: https, or http only for a loopback host. */
bool isAcceptableServerUrl(const juce::String& url);

/** True when `url` points at the official API server (synth::branding::kApiBaseUrl, and in Debug builds the
    AGENTSYNTH_LOCAL_API_URL override). Only that server may receive the account token or the device id. */
bool isOfficialServer(const juce::String& url);

/** A sign-in page the app may open: https on the API host or on agentsynth.app and its subdomains. */
bool isTrustedVerificationUri(const juce::String& url);

} // namespace synth::serverurl
