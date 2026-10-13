// Song steps: the plan request, and the streamed build that ends in an ordinary edit plan. The wire format, the
// events and the build state are in AI/SongSteps; this is only the orchestration, and where the final envelope meets
// the existing preview and apply path (AIIntegrationServiceProjectEdit.cpp).
#include "AIIntegrationService.h"

namespace synth {

using songsteps::EventType;

namespace {

constexpr int kStepsVersion = 1;

juce::String describeBuildFailure(const AIProvider::AIError& error) {
    const auto& text = error.message;
    return text.startsWith("Error: ") ? text.substring(7) : text;
}

} // namespace

AIProvider::RequestId AIIntegrationService::planSong(const juce::String& text, const juce::String& length,
                                                     const juce::String& detail, const juce::var& previousPlan,
                                                     SongPlanCallback callback) {
    auto fail = [&](AIProvider::AIErrorKind kind, const juce::String& message) {
        SongPlanOutcome outcome;
        outcome.error.kind = kind;
        outcome.error.message = message;
        if (callback)
            callback(outcome);
        return AIProvider::RequestId{};
    };
    if (!provider)
        return fail(AIProvider::AIErrorKind::Schema, "Error: No AI provider selected.");

    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("userPrompt", text);
    body->setProperty("stepsVersion", kStepsVersion);
    if (length.isNotEmpty())
        body->setProperty("length", length);
    if (detail.isNotEmpty())
        body->setProperty("detail", detail);
    if (previousPlan.isObject())
        body->setProperty("previousPlan", previousPlan);

    juce::WeakReference<AIIntegrationService> weakThis(this);
    return provider->sendRouteRequest("/v1/song/plan", juce::var(body.get()),
                                      [weakThis, callback](const AIProvider::AIResponse& response) {
                                          SongPlanOutcome outcome;
                                          outcome.error = response.error;
                                          if (response.success) {
                                              juce::String problem;
                                              if (songsteps::parsePlanReply(response.content, outcome.reply, problem)) {
                                                  outcome.kind = SongPlanOutcome::Kind::plan;
                                              } else {
                                                  outcome.error.kind = AIProvider::AIErrorKind::Schema;
                                                  outcome.error.message = "Error: " + problem;
                                              }
                                          } else if (response.error.httpStatus == 404) {
                                              // A service that predates the song routes: remember it and let the caller
                                              // use project.generate.
                                              if (auto* self = weakThis.get())
                                                  self->songRoutesMissing = true;
                                              outcome.kind = SongPlanOutcome::Kind::unsupported;
                                          }
                                          if (callback)
                                              callback(outcome);
                                      });
}

AIProvider::RequestId AIIntegrationService::buildSong(const juce::String& requestText,
                                                      const songsteps::PlanReply& reply, SongBuildCallbacks callbacks) {
    auto finishNow = [&](SongBuildOutcome::Kind kind, const juce::String& message) {
        SongBuildOutcome outcome;
        outcome.kind = kind;
        outcome.message = message;
        if (callbacks.onFinished)
            callbacks.onFinished(outcome);
        return AIProvider::RequestId{};
    };
    if (!provider)
        return finishNow(SongBuildOutcome::Kind::failed, "No AI provider selected.");

    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("stepsVersion", kStepsVersion);
    body->setProperty("plan", reply.plan.raw);
    body->setProperty("token", reply.token);

    // Shared between the line handler and the end-of-stream handler: whichever sees the terminal event first reports.
    struct Run {
        bool reported = false;
        bool sawError = false;
        bool refunded = false;
        juce::String errorMessage;
    };
    auto run = std::make_shared<Run>();
    juce::WeakReference<AIIntegrationService> weakThis(this);
    const juce::String title = reply.plan.title;

    auto report = [run, callbacks](const SongBuildOutcome& outcome) {
        if (run->reported)
            return;
        run->reported = true;
        if (callbacks.onFinished)
            callbacks.onFinished(outcome);
    };

    auto onLine = [weakThis, run, callbacks, report, requestText, title](const juce::String& line) {
        auto* self = weakThis.get();
        const auto event = songsteps::parseStreamLine(line);
        if (self == nullptr || !event || run->reported)
            return;
        if (callbacks.onEvent)
            callbacks.onEvent(*event);
        if (event->type == EventType::error) {
            run->sawError = true;
            run->refunded = event->refunded;
            run->errorMessage = event->message;
        } else if (event->type == EventType::done) {
            SongBuildOutcome outcome;
            outcome.done = *event;
            outcome.edit = self->applyProjectEdit(event->envelope);
            outcome.kind = outcome.edit.ok ? SongBuildOutcome::Kind::applied : SongBuildOutcome::Kind::rejected;
            outcome.message = outcome.edit.message;
            if (outcome.edit.ok) {
                self->chatHistory.push_back({"user", requestText});
                self->chatHistory.push_back({"assistant", "Built the song " + title + "."});
                self->trimHistory();
            }
            report(outcome);
        }
    };

    auto onFinished = [run, report](const AIProvider::AIResponse& response) {
        SongBuildOutcome outcome;
        if (run->sawError) {
            outcome.kind = SongBuildOutcome::Kind::failed;
            outcome.message = run->errorMessage;
            outcome.refunded = run->refunded;
        } else if (response.error.kind == AIProvider::AIErrorKind::Cancelled) {
            outcome.kind = SongBuildOutcome::Kind::cancelled;
        } else if (response.error.code == "PLAN_EXPIRED") {
            outcome.kind = SongBuildOutcome::Kind::expired;
            outcome.message = describeBuildFailure(response.error);
        } else if (response.error.kind == AIProvider::AIErrorKind::Quota || response.error.code == "QUOTA_EXCEEDED") {
            outcome.kind = SongBuildOutcome::Kind::quotaExceeded;
            outcome.message = describeBuildFailure(response.error);
        } else {
            outcome.kind = SongBuildOutcome::Kind::failed;
            outcome.message = response.success ? juce::String("The connection closed before the song was finished.")
                                               : describeBuildFailure(response.error);
            // A dropped connection ends the build on the server, which gives the uses back.
            outcome.refunded = response.success || response.error.kind == AIProvider::AIErrorKind::Network;
        }
        report(outcome);
    };

    return provider->streamRouteRequest("/v1/song/build", juce::var(body.get()), std::move(onLine),
                                        std::move(onFinished));
}

} // namespace synth
