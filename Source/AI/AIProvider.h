#pragma once

#include <functional>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/**
 * @class AIProvider
 * @brief Abstract interface for AI backends (Ollama, OpenAI, etc.)
 */
class AIProvider {
public:
    virtual ~AIProvider() = default;

    /**
     * @brief Structure representing a message in a conversation
     */
    struct Message {
        juce::String role; // "system", "user", or "assistant"
        juce::String content;
    };

    using CompletionCallback = std::function<void(const juce::String& response, bool success)>;

    /**
     * @brief Sends a prompt to the AI and calls the callback when the response is ready.
     * @param conversation The chat history
     * @param callback Callback for result
     * @param responseSchema Optional JSON schema for structured output
     */
    virtual void sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback,
                            const juce::var& responseSchema = juce::var()) = 0;

    /**
     * @brief Returns the name of the provider.
     */
    virtual juce::String getProviderName() const = 0;

    /**
     * @brief Sets the model to use for completion.
     */
    virtual void setModel(const juce::String& name) = 0;

    /**
     * @brief Returns the current model name.
     */
    virtual juce::String getCurrentModel() const = 0;

    /**
     * @brief Fetches all available models from the provider.
     */
    virtual void fetchAvailableModels(std::function<void(const juce::StringArray& models, bool success)> callback) = 0;
};

} // namespace synth
