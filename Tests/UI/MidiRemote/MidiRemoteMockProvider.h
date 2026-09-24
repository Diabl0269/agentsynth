#pragma once

// A MainComponent-based MIDI Remote test must not get the default Ollama provider: its async model
// fetch outlives the test and aborts inside AIChatComponent.
#include "AI/AIProvider.h"

// MainComponent must not get the default Ollama provider: its async model fetch outlives the test.
class MidiRemoteMockProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockPTO"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};
