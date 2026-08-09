#pragma once

#include <cstdint>
#include <functional>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/**
 * @class AIProvider
 * @brief Abstract interface for AI backends (local Ollama, hosted/authenticated/metered
 *        services, etc.)
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

    /**
     * @brief Identifies a single sendPrompt() request, so a provider can be asked to cancel it.
     */
    struct RequestId {
        uint64_t value = 0;

        bool operator==(const RequestId& other) const { return value == other.value; }
        bool operator!=(const RequestId& other) const { return value != other.value; }
    };

    /**
     * @brief Categorizes why a request failed, so callers can react appropriately
     *        (retry, prompt sign-in, show an upgrade path, etc.) instead of parsing error text.
     *
     * TrialExhausted and ServiceCapacityExceeded are both surfaced by the capability endpoints as
     * distinct 402/503 response shapes (see RemoteProvider::processRequest) and are deliberately
     * NOT folded into Quota/Server: TrialExhausted is the free-trial-to-account conversion moment
     * (the server's message invites signing in when the caller isn't authenticated yet), and
     * ServiceCapacityExceeded is a service-wide daily cap unrelated to the caller's own usage —
     * both need their own user-facing text rather than a generic failure message.
     */
    enum class AIErrorKind {
        None,
        Network,
        Auth,
        Quota,
        RateLimit,
        Server,
        Schema,
        Cancelled,
        Timeout,
        TrialExhausted,
        ServiceCapacityExceeded
    };

    struct AIError {
        AIErrorKind kind = AIErrorKind::None;
        juce::String message;
        int retryAfterSeconds = 0;
        juce::String requestId;
    };

    /**
     * @brief Single result payload delivered to a CompletionCallback.
     */
    struct AIResponse {
        bool success = false;
        juce::String content;
        AIError error;
        juce::String requestId;
    };

    using CompletionCallback = std::function<void(const AIResponse& response)>;

    /**
     * @brief Sends a prompt to the AI and calls the callback when the response is ready.
     * @param conversation The chat history
     * @param callback Callback for result
     * @param responseSchema Optional JSON schema for structured output
     * @param onDelta Optional streaming callback, invoked with incremental content as it
     *                arrives. Providers that cannot stream simply never call it.
     * @return An id identifying this request, usable with cancel().
     */
    virtual RequestId sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback,
                                 const juce::var& responseSchema = juce::var(),
                                 std::function<void(const juce::String& delta)> onDelta = {}) = 0;

    /**
     * @brief Requests cancellation of a previously started sendPrompt(). Providers that cannot
     *        cancel in-flight requests may implement this as a no-op.
     */
    virtual void cancel(RequestId requestId) = 0;

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

    /**
     * @brief Sets the authentication token for hosted/authenticated backends. No-op by
     *        default, so providers with no notion of auth (e.g. local Ollama) are unaffected.
     */
    virtual void setAuthToken(const juce::String&) {}
};

} // namespace synth
