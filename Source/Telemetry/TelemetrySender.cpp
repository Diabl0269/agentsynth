#include "TelemetrySender.h"
#include "Branding.h"
#include <juce_events/juce_events.h>
#include <thread>
#include <vector>

// Concern: the launch-time send. Bodies are built on the message thread, the requests run on one
// background thread, and each outcome is posted back so the queue is only ever touched on the message thread.

namespace synth::telemetry {

namespace {

constexpr int kRequestTimeoutMs = 10000;

struct PendingRequest {
    juce::String day;
    juce::String body;
};

/** A summary leaves the queue once the server has answered for good: any 2xx, or a 4xx other than 429 (the same
    body would be refused again). A 429, a 5xx or a failed transfer keeps it for the next launch. */
bool serverHasAnsweredForGood(const AuthClient::HttpResult& result) {
    if (result.transportFailed || result.timedOut)
        return false;
    const int status = result.httpStatus;
    return (status >= 200 && status < 300) || (status >= 400 && status < 500 && status != 429);
}

} // namespace

TelemetrySender::TelemetrySender(AuthClient::HttpPerformer performerIn, juce::String apiBaseUrl,
                                 Dispatcher dispatcherIn, MainThreadPoster posterIn)
    : performer(performerIn ? std::move(performerIn) : AuthClient::defaultHttpPerformer())
    , baseUrl(apiBaseUrl.isNotEmpty() ? std::move(apiBaseUrl) : juce::String(synth::branding::resolveApiBaseUrl()))
    , dispatcher(dispatcherIn ? std::move(dispatcherIn)
                              : Dispatcher([](Work work) { std::thread(std::move(work)).detach(); }))
    , poster(posterIn ? std::move(posterIn)
                      : MainThreadPoster([](Work work) { juce::MessageManager::callAsync(std::move(work)); })) {}

/** Closing the app mid-send: the flag stops the request in flight, and `alive` turns every result still queued for
    the message thread into a no-op, so nothing touches the recorder after its owner is gone. */
TelemetrySender::~TelemetrySender() {
    alive->store(false);
    cancelled->store(true);
}

void TelemetrySender::sendPending(TelemetryRecorder& recorder) {
    if (!recorder.isEnabled() || inFlight->load())
        return;
    const auto days = recorder.getPendingDays();
    if (days.empty())
        return;

    const auto context = recorder.makeContext();
    std::vector<PendingRequest> requests;
    for (const auto& day : days)
        requests.push_back({day.day, serializeDaily(toDaily(day, context))});

    inFlight->store(true);
    const auto generation = recorder.getOptInGeneration();
    const int generationAtStart = generation->load();
    const auto id = context.telemetryId;
    const auto url = baseUrl + kEndpointPath;
    auto work = [performer = performer, poster = poster, alive = alive, cancelled = cancelled, inFlight = inFlight,
                 generation, generationAtStart, recorderPtr = &recorder, requests = std::move(requests), id, url]() {
        for (const auto& request : requests) {
            if (cancelled->load() || generation->load() != generationAtStart)
                break; // closing, or the user opted out (or back in) since this send began
            juce::StringPairArray headers;
            headers.set("Content-Type", "application/json");
            const auto result = performer("POST", url, headers, request.body, kRequestTimeoutMs, *cancelled);
            if (!serverHasAnsweredForGood(result))
                continue;
            poster([alive, recorderPtr, day = request.day, id] {
                if (alive->load())
                    recorderPtr->removeSentDay(day, id);
            });
        }
        poster([inFlight] { inFlight->store(false); });
    };
    dispatcher(std::move(work));
}

} // namespace synth::telemetry
