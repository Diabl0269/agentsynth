#pragma once

#include "AIProvider.h"
#include <atomic>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <thread>
#include <vector>

namespace synth {

/**
 * @class OllamaProvider
 * @brief AI provider implementation for local Ollama instances.
 */
class OllamaProvider
    : public AIProvider
    , protected juce::Thread {
public:
    using InputStreamFactory =
        std::function<std::unique_ptr<juce::InputStream>(const juce::URL&, const juce::URL::InputStreamOptions&)>;

    OllamaProvider(const juce::String& host = "http://localhost:11434");

    // Test-specific constructor to inject a mock input stream factory
    OllamaProvider(const juce::String& host, InputStreamFactory streamFactory)
        : Thread("OllamaProviderThread")
        , ollamaHost(host)
        , createStream(std::move(streamFactory)) {}

    ~OllamaProvider() override {
        stopThread(2000);
        // Join the discovery worker so it cannot touch our members after we are
        // destroyed (it captures `this`). With the short 4s connection timeout
        // and the in-flight guard ensuring at most ONE worker exists, this join
        // can block for at most ~4s in the worst case (server unreachable), never
        // the old 120s. We never reassign a joinable thread (the worker is only
        // launched when none is in flight), so std::terminate cannot fire here.
        if (discoveryThread.joinable())
            discoveryThread.join();
    }

    void sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback,
                    const juce::var& responseSchema = juce::var()) override {
        {
            const juce::ScopedLock sl(queueLock);
            pendingRequests.push_back({conversation, callback, responseSchema});
        }

        if (!isThreadRunning())
            startThread();
    }

    juce::String getProviderName() const override { return "Ollama"; }

    using juce::Thread::stopThread; // Make stopThread public for testing purposes

    void setModel(const juce::String& name) override;
    juce::String getCurrentModel() const override;
    void fetchAvailableModels(std::function<void(const juce::StringArray& models, bool success)> callback) override;

    void setTestMode(bool testMode) { isTestMode = testMode; }

private:
    juce::String ollamaHost;
    juce::String currentModel;
    InputStreamFactory createStream; // Member variable for the stream factory
    bool isTestMode = false;
    std::thread discoveryThread;
    // Guards against (a) blocking the caller (we never join on the message thread)
    // and (b) ever having two discovery workers alive at once. Set true before a
    // worker launches, cleared by the worker when it finishes.
    std::atomic<bool> discoveryInFlight{false};

    struct Request {
        std::vector<Message> conversation;
        AIProvider::CompletionCallback callback;
        juce::var responseSchema;
    };

    juce::CriticalSection queueLock;
    std::vector<Request> pendingRequests;

    void run() override;                     // Declaration for inherited method
    void processRequest(const Request& req); // Declaration for private method

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OllamaProvider)
};

} // namespace synth
