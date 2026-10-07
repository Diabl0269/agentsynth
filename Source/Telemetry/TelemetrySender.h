#pragma once

#include "AI/AuthClient.h"
#include "TelemetryRecorder.h"
#include <atomic>
#include <functional>
#include <memory>

namespace synth::telemetry {

/**
 * @class TelemetrySender
 * @brief Posts the days before today, once, at launch, without ever blocking or failing the app.
 *
 * Each request carries only a JSON content type: no account token, no device id, nothing that links a
 * summary to a person. Send results come back to the message thread, where the sent day leaves the
 * queue. Docs: docs/development/usage-statistics.md.
 */
class TelemetrySender {
public:
    using Work = std::function<void()>;
    /** Runs the work off the message thread (default: a detached thread). */
    using Dispatcher = std::function<void(Work)>;
    /** Runs the work on the message thread (default: MessageManager::callAsync). */
    using MainThreadPoster = std::function<void(Work)>;

    /** `performer` null means the real network transport. `apiBaseUrl` empty means branding::resolveApiBaseUrl(). */
    explicit TelemetrySender(AuthClient::HttpPerformer performer = nullptr, juce::String apiBaseUrl = {},
                             Dispatcher dispatcher = nullptr, MainThreadPoster poster = nullptr);
    ~TelemetrySender();

    /** Message thread only. Does nothing while the recorder is disabled or a send is still running. */
    void sendPending(TelemetryRecorder& recorder);

    /** The request path under the API base. */
    static constexpr const char* kEndpointPath = "/v1/telemetry/daily";

private:
    AuthClient::HttpPerformer performer;
    juce::String baseUrl;
    Dispatcher dispatcher;
    MainThreadPoster poster;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> inFlight = std::make_shared<std::atomic<bool>>(false);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TelemetrySender)
};

} // namespace synth::telemetry
