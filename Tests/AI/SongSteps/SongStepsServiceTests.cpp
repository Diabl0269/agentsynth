// The song steps through AIIntegrationService with a scripted provider: plan, then build with the final envelope
// applied as the existing edit plan, plus the silent fallback when the service has no song routes.
#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/PatchEval.h"
#include <gtest/gtest.h>

namespace synth {

namespace {

const char* kPlanData = R"({"plan":{"title":"Dark drum and bass","bpm":174,"key":"A minor","special":"",
    "sections":[{"name":"Intro","bars":16,"energy":1,"chords":"Am","description":"pad"}],
    "tracks":[{"name":"Kit","role":"drums","instrument":"Drum Kit","sound":"punchy","sections":["Intro"]}],"lanes":[]},
    "price":{"units":3,"estimatedUsd":0.01},"steps":[{"id":"track:Kit","kind":"track","label":"Kit sound"}],"token":"tok"})";

const char* kEnvelope = R"({"mode":"merge","nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Audio Output"}],
    "connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0}]})";

class SongProvider : public AIProvider {
public:
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        return {};
    }
    RequestId sendRouteRequest(const juce::String& route, const juce::var& body, CompletionCallback cb) override {
        routes.push_back(route);
        lastPlanBody = body;
        AIResponse response;
        response.success = planStatus == 200;
        response.content = kPlanData;
        response.error.httpStatus = planStatus == 200 ? 0 : planStatus;
        response.error.kind = planStatus == 200 ? AIErrorKind::None : AIErrorKind::Schema;
        cb(response);
        return {1};
    }
    RequestId streamRouteRequest(const juce::String& route, const juce::var& body, StreamLineCallback onLine,
                                 CompletionCallback onFinished) override {
        routes.push_back(route);
        lastBuildBody = body;
        for (const auto& line : lines)
            onLine(line);
        AIResponse response;
        response.success = finish.success;
        response.error = finish.error;
        onFinished(response);
        return {2};
    }
    void cancel(RequestId id) override { cancelled.push_back(id.value); }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> cb) override { cb({}, true); }
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }
    void setRequestTimeoutMs(int) override {}
    int getRequestTimeoutMs() const override { return 1000; }
    juce::String getProviderName() const override { return "SongProvider"; }
    bool isHosted() const override { return true; }

    int planStatus = 200;
    std::vector<juce::String> lines;
    AIResponse finish{true, {}, {}, {}, {}, {}};
    std::vector<juce::String> routes;
    juce::var lastPlanBody, lastBuildBody;
    std::vector<uint64_t> cancelled;
};

class SongStepsServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        graph = std::make_unique<juce::AudioProcessorGraph>();
        service = std::make_unique<AIIntegrationService>(*graph);
        auto owned = std::make_unique<SongProvider>();
        provider = owned.get();
        service->setProvider(std::move(owned));
    }

    songsteps::PlanReply plan() {
        songsteps::PlanReply reply;
        juce::String error;
        EXPECT_TRUE(songsteps::parsePlanReply(kPlanData, reply, error)) << error;
        return reply;
    }

    AIIntegrationService::SongBuildOutcome build() {
        AIIntegrationService::SongBuildOutcome outcome;
        int finished = 0;
        AIIntegrationService::SongBuildCallbacks callbacks;
        callbacks.onEvent = [this](const songsteps::StreamEvent& e) { events.push_back(e); };
        callbacks.onFinished = [&](const AIIntegrationService::SongBuildOutcome& o) {
            outcome = o;
            ++finished;
        };
        service->buildSong("dark drum and bass", plan(), callbacks);
        EXPECT_EQ(finished, 1) << "the build finishes exactly once";
        return outcome;
    }

    static juce::String doneLine() {
        return juce::String(R"({"type":"done","envelope":)") + kEnvelope +
               R"(,"notes":[],"summary":{"tracks":1,"sections":1,"bars":16,"seconds":22}})";
    }

    std::unique_ptr<juce::AudioProcessorGraph> graph;
    std::unique_ptr<AIIntegrationService> service;
    SongProvider* provider = nullptr;
    std::vector<songsteps::StreamEvent> events;
};

} // namespace

TEST_F(SongStepsServiceTest, PlanThenBuildHappyPathAppliesTheEnvelopeAndKeepsTheRoutesAndToken) {
    EXPECT_TRUE(service->canPlanSongs());
    AIIntegrationService::SongPlanOutcome planned;
    service->planSong("dark drum and bass", "medium", "full", {}, [&](const auto& o) { planned = o; });
    ASSERT_EQ(planned.kind, AIIntegrationService::SongPlanOutcome::Kind::plan);
    EXPECT_EQ(planned.reply.units, 3);
    EXPECT_EQ(provider->routes[0], juce::String("/v1/song/plan"));
    EXPECT_EQ(provider->lastPlanBody["userPrompt"].toString(), juce::String("dark drum and bass"));
    EXPECT_EQ(provider->lastPlanBody["length"].toString(), juce::String("medium"));
    EXPECT_EQ(provider->lastPlanBody["detail"].toString(), juce::String("full"));
    EXPECT_EQ((int)provider->lastPlanBody["stepsVersion"], 1);
    EXPECT_FALSE(provider->lastPlanBody.hasProperty("previousPlan"));
    // The instruments this app can build, so the service offers only those.
    ASSERT_TRUE(provider->lastPlanBody["instruments"].isArray());
    EXPECT_TRUE(provider->lastPlanBody["instruments"].getArray()->contains(juce::var("Noise")));
    EXPECT_TRUE((bool)provider->lastPlanBody["sounds"]) << "the plan may name core pack instruments";

    provider->lines = {R"({"type":"steps","steps":[{"id":"track:Kit","kind":"track","label":"Kit sound"}]})",
                       R"({"type":"step","id":"track:Kit","status":"running"})",
                       "not json",
                       R"({"type":"ping"})",
                       R"({"type":"step","id":"track:Kit","status":"done","ms":5})",
                       doneLine()};
    const auto outcome = build();

    EXPECT_EQ(outcome.kind, AIIntegrationService::SongBuildOutcome::Kind::applied) << outcome.message;
    EXPECT_TRUE(outcome.edit.ok);
    EXPECT_EQ(events.size(), 5u) << "the malformed line is dropped, the rest are forwarded in order";
    EXPECT_EQ(events.back().type, songsteps::EventType::done);
    EXPECT_EQ(provider->routes[1], juce::String("/v1/song/build"));
    ASSERT_TRUE(provider->lastBuildBody["instruments"].isArray());
    EXPECT_TRUE(provider->lastBuildBody["instruments"].getArray()->contains(juce::var("Noise")));
    EXPECT_TRUE((bool)provider->lastBuildBody["sounds"]) << "the build may carry a sound per instrument track";
    EXPECT_EQ(provider->lastBuildBody["token"].toString(), juce::String("tok"));
    EXPECT_EQ(provider->lastBuildBody["plan"]["title"].toString(), juce::String("Dark drum and bass"));
    EXPECT_EQ(graph->getNumNodes(), 2) << "the envelope went through the ordinary apply path";
    ASSERT_GE(service->getHistory().size(), 2u);
    EXPECT_EQ(service->getHistory().back().role, juce::String("assistant"));
}

TEST_F(SongStepsServiceTest, ARePlanSendsThePreviousPlanAsAChangeRequest) {
    service->planSong("fewer tracks", "", "light", plan().plan.raw, [](const auto&) {});
    EXPECT_TRUE(provider->lastPlanBody["previousPlan"].isObject());
    EXPECT_FALSE(provider->lastPlanBody.hasProperty("length"));
}

TEST_F(SongStepsServiceTest, A404OnPlanMeansUnsupportedAndStopsOfferingSongs) {
    provider->planStatus = 404;
    AIIntegrationService::SongPlanOutcome planned;
    service->planSong("a song", "", "", {}, [&](const auto& o) { planned = o; });
    EXPECT_EQ(planned.kind, AIIntegrationService::SongPlanOutcome::Kind::unsupported);
    EXPECT_FALSE(service->canPlanSongs()) << "later requests go straight to project.generate";
}

TEST_F(SongStepsServiceTest, AnotherFailureOnPlanIsReportedAsFailedAndSongsStayAvailable) {
    provider->planStatus = 502;
    AIIntegrationService::SongPlanOutcome planned;
    service->planSong("a song", "", "", {}, [&](const auto& o) { planned = o; });
    EXPECT_EQ(planned.kind, AIIntegrationService::SongPlanOutcome::Kind::failed);
    EXPECT_TRUE(service->canPlanSongs());
}

TEST_F(SongStepsServiceTest, AnErrorLineEndsTheBuildAsFailedWithTheRefund) {
    provider->lines = {R"({"type":"error","message":"The build failed","refunded":true})"};
    const auto outcome = build();
    EXPECT_EQ(outcome.kind, AIIntegrationService::SongBuildOutcome::Kind::failed);
    EXPECT_TRUE(outcome.refunded);
    EXPECT_EQ(outcome.message, juce::String("The build failed"));
    EXPECT_EQ(graph->getNumNodes(), 0);
}

TEST_F(SongStepsServiceTest, AStreamThatEndsWithoutAFinalLineIsAFailureNotASilentSuccess) {
    provider->lines = {R"({"type":"step","id":"track:Kit","status":"running"})"};
    const auto outcome = build();
    EXPECT_EQ(outcome.kind, AIIntegrationService::SongBuildOutcome::Kind::failed);
    EXPECT_TRUE(outcome.refunded);
}

TEST_F(SongStepsServiceTest, ExpiredQuotaAndCancelledHaveTheirOwnOutcomes) {
    provider->finish.success = false;
    provider->finish.error.code = "PLAN_EXPIRED";
    provider->finish.error.message = "Error: That plan has expired.";
    EXPECT_EQ(build().kind, AIIntegrationService::SongBuildOutcome::Kind::expired);

    provider->finish.error = {};
    provider->finish.error.kind = AIProvider::AIErrorKind::Quota;
    provider->finish.error.message = "Out of uses.";
    const auto quota = build();
    EXPECT_EQ(quota.kind, AIIntegrationService::SongBuildOutcome::Kind::quotaExceeded);
    EXPECT_EQ(quota.message, juce::String("Out of uses."));

    provider->finish.error = {};
    provider->finish.error.kind = AIProvider::AIErrorKind::Cancelled;
    EXPECT_EQ(build().kind, AIIntegrationService::SongBuildOutcome::Kind::cancelled);
}

TEST_F(SongStepsServiceTest, ADoneEnvelopeTheAppRefusesIsRejectedNotAppliedAndTheModelStillSawDone) {
    provider->lines = {
        R"({"type":"done","envelope":{"mode":"merge","nodes":[{"id":1,"type":"NoSuchModule"}]},"notes":[]})"};
    const auto outcome = build();
    EXPECT_EQ(outcome.kind, AIIntegrationService::SongBuildOutcome::Kind::rejected);
    EXPECT_FALSE(outcome.message.isEmpty());
    EXPECT_EQ(graph->getNumNodes(), 0);
}

TEST_F(SongStepsServiceTest, ALocalProviderDoesNotOfferSongSteps) {
    class Local : public SongProvider {
    public:
        bool isHosted() const override { return false; }
    };
    service->setProvider(std::make_unique<Local>());
    EXPECT_FALSE(service->canPlanSongs());
}

} // namespace synth
