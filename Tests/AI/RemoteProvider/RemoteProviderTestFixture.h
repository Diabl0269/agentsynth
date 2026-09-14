// Shared fixture: mock callbacks, the bounded-wait latch, HTTP result builders, and the
// RemoteProviderTest test-class used across every RemoteProvider*Tests.cpp topic file.
#pragma once

#include "AI/RemoteProvider.h"
#include "Branding.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>

namespace {

struct MockCompletionCallback {
    std::promise<std::pair<juce::String, bool>> promise;

    void operator()(const juce::StringArray& models, bool success) {
        promise.set_value({models.joinIntoString("|"), success});
    }

    std::pair<juce::String, bool> getResult() { return promise.get_future().get(); }
};

struct MockPromptCallback {
    std::promise<synth::AIProvider::AIResponse> promise;

    void operator()(const synth::AIProvider::AIResponse& response) { promise.set_value(response); }

    synth::AIProvider::AIResponse getResult() { return promise.get_future().get(); }
};

// Bounded-wait latch for "did the callback fire?" assertions. See OllamaProviderTests.cpp for the
// identical rationale: every wait has a timeout so a lost request fails the test instead of
// hanging it.
class CallbackLatch {
public:
    void fire() {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            fired = true;
        }
        cv.notify_all();
    }

    bool waitFor(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, timeout, [this] { return fired; });
    }

    bool hasFired() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return fired;
    }

private:
    mutable std::mutex mutex;
    std::condition_variable cv;
    bool fired = false;
};

constexpr std::chrono::milliseconds kCallbackTimeout{10000};

const juce::String kMockHost = "http://mock-host:8787";

// A non-void response schema, matching what AIIntegrationService::sendMessage() passes for a
// structured patch request (AIStateMapper::getPatchSchema()). Content is irrelevant to
// RemoteProvider — it never sends "format" the way OllamaProvider does — only isVoid() matters.
juce::var makeSchema() { return juce::var(new juce::DynamicObject()); }

synth::RemoteProvider::HttpResult makeSuccess(const juce::String& body) {
    synth::RemoteProvider::HttpResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}

synth::RemoteProvider::HttpResult makeStatus(int status, const juce::String& body = "{}",
                                             const juce::StringPairArray& headers = {}) {
    synth::RemoteProvider::HttpResult result;
    result.httpStatus = status;
    result.body = body;
    result.headers = headers;
    return result;
}

} // namespace

class RemoteProviderTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Nothing persistent between tests: each test constructs its own provider.
    }
};
