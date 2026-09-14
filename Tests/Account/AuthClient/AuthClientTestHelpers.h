// Shared fixture: mock host/client id, HTTP result builders, and the form-body parser used across
// every AuthClient*Tests.cpp topic file.
#pragma once

#include "AI/AuthClient.h"
#include <atomic>
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <map>

namespace {

const juce::String kHost = "http://mock-host:8787";
const juce::String kClientId = "synth-desktop";
const juce::String kDeviceId = "device-uuid-1234";

synth::AuthClient::HttpResult makeStatus(int status, const juce::String& body,
                                         const juce::StringPairArray& headers = {}) {
    synth::AuthClient::HttpResult result;
    result.httpStatus = status;
    result.body = body;
    result.headers = headers;
    return result;
}

synth::AuthClient::HttpResult makeTransportFailure(const juce::String& message = "Could not resolve host") {
    synth::AuthClient::HttpResult result;
    result.transportFailed = true;
    result.errorMessage = message;
    return result;
}

std::atomic<bool> kNeverCancelled{false};

// Parses a application/x-www-form-urlencoded body into a key -> decoded-value map, for asserting
// on the exact fields a request sent.
std::map<juce::String, juce::String> parseForm(const juce::String& body) {
    std::map<juce::String, juce::String> result;
    for (const auto& pair : juce::StringArray::fromTokens(body, "&", "")) {
        const int eq = pair.indexOfChar('=');
        if (eq < 0)
            continue;
        const auto key = pair.substring(0, eq);
        const auto value = juce::URL::removeEscapeChars(pair.substring(eq + 1));
        result[key] = value;
    }
    return result;
}

} // namespace
