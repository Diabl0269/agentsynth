#pragma once

#include "AIProvider.h"
#include "AIStateMapper.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace gsynth {

/**
 * @class AIIntegrationService
 * @brief Orchestrates AI interactions and bridges them with the synth engine.
 */
class AIIntegrationService {
public:
    AIIntegrationService(juce::AudioProcessorGraph& graph);
    ~AIIntegrationService();

    /**
     * @class Listener
     * @brief Interface for observing AI-driven changes to the synthesizer state.
     */
    class Listener {
    public:
        virtual ~Listener() = default;
        // Called BEFORE the patch is applied to the graph — listeners must tear down any UI that holds
        // references into the graph's processors (e.g. detach module components so ScopeComponent timers
        // stop) before the old processors/VisualBuffers are freed. Default no-op for listeners that don't.
        virtual void aiPatchAboutToApply() {}
        virtual void aiPatchApplied() = 0;
    };

    void addListener(Listener* l) { listeners.add(l); }
    void removeListener(Listener* l) { listeners.remove(l); }

    void setProvider(std::unique_ptr<AIProvider> newProvider);

    /**
     * @brief Sends a user message and gets a response.
     */
    void sendMessage(const juce::String& text, AIProvider::CompletionCallback callback,
                     bool useStructuredOutput = false);

    /**
     * @brief Applies a JSON patch to the graph.
     */
    bool applyPatch(const juce::String& jsonString, bool mergeMode = false);

    /**
     * @brief Returns the current graph state as a JSON string for context.
     */
    juce::String getPatchContext();

    /**
     * @brief Returns the chat history.
     */
    const std::vector<AIProvider::Message>& getHistory() const { return chatHistory; }

    /**
     * @brief Clears the chat history (except system prompt).
     */
    void clearHistory();

    /**
     * @brief Model management methods.
     */
    void setModel(const juce::String& name);
    juce::String getCurrentModel() const;
    void fetchAvailableModels(std::function<void(const juce::StringArray& models, bool success)> callback);

private:
    std::unique_ptr<AIProvider> provider;
    std::vector<AIProvider::Message> chatHistory;
    juce::AudioProcessorGraph& audioGraph;
    juce::ListenerList<Listener> listeners;

    void initSystemPrompt();

    /**
     * @brief Helper to extract JSON from a response that might contain conversational text.
     */
    juce::String extractJsonFromResponse(const juce::String& response);

    JUCE_DECLARE_WEAK_REFERENCEABLE(AIIntegrationService)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIIntegrationService)
};

} // namespace gsynth
