// A real song over the wire, skipped unless AGENTSYNTH_SONG_LIVE_URL names a running service (a local API with
// /v1/song/*). It spends real model calls, so it never runs in CI: plan, then build, then the envelope applied through
// the same service a chat uses, with every event printed.
#include "../AIIntegrationService/PlanFakeHost.h"
#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/RemoteProvider.h"
#include "AudioEngine/AudioEngine.h"
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>

namespace {

template <class Done>
bool pumpUntil(Done done, int timeoutMs) {
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32)timeoutMs;
    while (!done() && juce::Time::getMillisecondCounter() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    return done();
}

} // namespace

TEST(SongStepsLive, PlanThenBuildAgainstARunningService) {
    const char* url = std::getenv("AGENTSYNTH_SONG_LIVE_URL");
    if (url == nullptr)
        GTEST_SKIP() << "set AGENTSYNTH_SONG_LIVE_URL to run against a service";
    const char* promptEnv = std::getenv("AGENTSYNTH_SONG_LIVE_PROMPT");
    const juce::String prompt = promptEnv != nullptr ? promptEnv : "dark drum and bass";

    AudioEngine engine;
    synth::AIIntegrationService service{engine.getGraph()};
    synth::TimelineDoc doc;
    synth::TransportService transport;
    synth::PlanFakeHost host{doc, engine.getGraph()};
    service.setProvider(std::make_unique<synth::RemoteProvider>(url));
    service.setTimelineContext(&doc, &transport);
    service.setTimelineToolsEnabled(true);
    service.setTimelineOpsHost(&host);

    bool planned = false;
    synth::AIIntegrationService::SongPlanOutcome plan;
    service.planSong(prompt, "short", "light", {}, [&](const auto& o) {
        plan = o;
        planned = true;
    });
    ASSERT_TRUE(pumpUntil([&] { return planned; }, 120000));
    ASSERT_EQ(plan.kind, synth::AIIntegrationService::SongPlanOutcome::Kind::plan) << plan.error.message;
    std::cerr << "[live] plan: " << plan.reply.plan.title << ", " << synth::songsteps::planMetaText(plan.reply.plan)
              << ", " << plan.reply.plan.tracks.size() << " tracks, price " << plan.reply.units << " uses (~$"
              << plan.reply.estimatedUsd << ")\n";

    synth::songsteps::BuildModel model;
    bool finished = false;
    synth::AIIntegrationService::SongBuildOutcome outcome;
    synth::AIIntegrationService::SongBuildCallbacks callbacks;
    callbacks.onEvent = [&](const synth::songsteps::StreamEvent& e) {
        model.apply(e);
        if (e.type == synth::songsteps::EventType::step)
            std::cerr << "[live] step " << e.stepId << " " << synth::songsteps::stepStatusToString(e.status) << " "
                      << e.ms << " ms, finished " << model.finishedCount() << "/" << model.total() << "\n";
        else if (e.type == synth::songsteps::EventType::steps)
            std::cerr << "[live] " << e.steps.size() << " steps announced\n";
    };
    callbacks.onFinished = [&](const auto& o) {
        outcome = o;
        finished = true;
    };
    service.buildSong(prompt, plan.reply, callbacks);
    ASSERT_TRUE(pumpUntil([&] { return finished; }, 600000));
    std::cerr << "[live] outcome " << (int)outcome.kind << " " << outcome.message << "; " << model.resultLine()
              << "; tracks in doc " << doc.getTracks().size() << "\n";
    EXPECT_EQ(outcome.kind, synth::AIIntegrationService::SongBuildOutcome::Kind::applied) << outcome.message;
    EXPECT_GT(doc.getTracks().size(), 0u);
}
