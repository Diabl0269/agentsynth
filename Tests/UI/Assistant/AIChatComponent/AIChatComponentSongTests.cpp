// The song path through the chat: a song request becomes a plan card with nothing built, Length and Detail re-plan,
// Build streams the steps card and applies the envelope, Cancel closes the stream, and a service without the routes
// falls back to the single project request without a word about it.
#include "../../../AI/AIIntegrationService/PlanFakeHost.h"
#include "../../../SoundPacks/SoundPackTestKit.h"
#include "../../../TestWait.h"
#include "AIChatComponentTestFixture.h"
#include "UI/Assistant/AIChatComponent/BuildStepsCard.h"
#include "UI/Assistant/AIChatComponent/SongPlanCard.h"

namespace {

const char* kPlanData = R"({"plan":{"title":"Dark drum and bass","bpm":174,"key":"A minor","special":"",
    "sections":[{"name":"Intro","bars":16,"energy":1,"chords":"Am","description":"pad"},
                {"name":"Drop","bars":32,"energy":5,"chords":"Am","description":"full"}],
    "tracks":[{"name":"Kit","role":"drums","instrument":"Drum Kit","sound":"x","sections":["Intro","Drop"]}],"lanes":[]},
    "price":{"units":5,"estimatedUsd":0.02},"steps":[{"id":"track:Kit","kind":"track","label":"Kit sound"}],"token":"tok"})";

const char* kRePlanData = R"({"plan":{"title":"Dark drum and bass","bpm":174,"key":"A minor","special":"",
    "sections":[{"name":"Intro","bars":16,"energy":1,"chords":"Am","description":"pad"},
                {"name":"Drop","bars":64,"energy":5,"chords":"Am","description":"full"}],
    "tracks":[{"name":"Kit","role":"drums","instrument":"Drum Kit","sound":"x","sections":["Intro","Drop"]}],"lanes":[]},
    "price":{"units":8,"estimatedUsd":0.03},"steps":[{"id":"track:Kit","kind":"track","label":"Kit sound"}],"token":"tok2"})";

const char* kEnvelope = R"({"mode":"merge","nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Audio Output"}],
    "connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0}]})";

class SongProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "SongProvider"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> cb) override { cb({}, true); }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        return {};
    }
    RequestId sendCapabilityRequest(const juce::String& capability, const juce::var&, CompletionCallback cb) override {
        capabilityCalls.push_back(capability);
        AIResponse response;
        response.success = true;
        response.content = R"({"nodes":[],"connections":[]})";
        cb(response);
        return {};
    }
    RequestId sendRouteRequest(const juce::String& route, const juce::var& body, CompletionCallback cb) override {
        routes.push_back(route);
        planBodies.push_back(body);
        if (holdPlans) { // the network is slow: the answer arrives when the test says
            heldPlan = std::move(cb);
            return {7};
        }
        AIResponse response;
        if (planStatus == 200) {
            response.success = true;
            response.content = planAnswers.empty() ? kPlanData : planAnswers.front();
            if (!planAnswers.empty())
                planAnswers.erase(planAnswers.begin());
        } else {
            response.error.kind = AIErrorKind::Schema;
            response.error.httpStatus = planStatus;
            response.error.message = "Error: nope";
        }
        cb(response);
        return {7};
    }
    RequestId streamRouteRequest(const juce::String& route, const juce::var& body, StreamLineCallback onLine,
                                 CompletionCallback onFinished) override {
        routes.push_back(route);
        buildBody = body;
        this->onLine = std::move(onLine);
        this->onFinished = std::move(onFinished);
        return {9};
    }
    void cancel(RequestId id) override {
        cancelled.push_back(id.value);
        if (id.value == 9 && onFinished) {
            AIResponse response;
            response.error.kind = AIErrorKind::Cancelled;
            auto cb = std::move(onFinished);
            onFinished = nullptr;
            cb(response);
        }
    }
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }
    void setRequestTimeoutMs(int) override {}
    int getRequestTimeoutMs() const override { return 240000; }
    bool isHosted() const override { return hosted; }

    void line(const juce::String& json) { onLine(json); }
    void end(bool ok = true) {
        synth::AIProvider::AIResponse response;
        response.success = ok;
        auto cb = std::move(onFinished);
        onFinished = nullptr;
        cb(response);
    }

    bool hosted = true;
    bool holdPlans = false;
    CompletionCallback heldPlan;
    int planStatus = 200;
    std::vector<juce::String> planAnswers;
    std::vector<juce::String> routes, capabilityCalls;
    std::vector<juce::var> planBodies;
    juce::var buildBody;
    std::vector<uint64_t> cancelled;
    StreamLineCallback onLine;
    CompletionCallback onFinished;
};

struct SongRig {
    SongRig() {
        auto owned = std::make_unique<SongProvider>();
        provider = owned.get();
        service.setProvider(std::move(owned));
        service.setTimelineContext(&doc, &transport);
        service.setTimelineToolsEnabled(true);
        service.setTimelineOpsHost(&host);
        juce::PropertiesFile::Options options;
        options.applicationName = "AIChatSongTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters(options);
        chat = std::make_unique<synth::AIChatComponent>(service, props);
        historyDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("chat-song-" + juce::Uuid().toString());
        chat->setLocalHistoryDirectoryForTesting(historyDir);
        chat->setSize(400, 700);
        chat->refreshModels();
    }

    void send(const juce::String& text) {
        for (auto* child : chat->getChildren())
            if (auto* editor = dynamic_cast<juce::TextEditor*>(child); editor != nullptr && editor->isVisible())
                editor->setText(text);
        chat->triggerSend();
        pump();
    }
    static void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(100); }

    synth::SongPlanCard* planCard() {
        return dynamic_cast<synth::SongPlanCard*>(chat->getSongCardForTesting(chat->getMessageCountForTesting() - 1));
    }
    synth::BuildStepsCard* buildCard() {
        return dynamic_cast<synth::BuildStepsCard*>(chat->getSongCardForTesting(chat->getMessageCountForTesting() - 1));
    }

    AudioEngine engine;
    synth::AIIntegrationService service{engine.getGraph()};
    synth::TimelineDoc doc;
    synth::TransportService transport;
    synth::PlanFakeHost host{doc, engine.getGraph()};
    juce::ApplicationProperties props;
    juce::File historyDir;
    SongProvider* provider = nullptr;
    std::unique_ptr<synth::AIChatComponent> chat;
};

} // namespace

TEST(SongRouting, WholeSongRequestsAreSongsAndEditsAreNot) {
    const juce::StringArray modules{"Filter", "Reverb", "Delay"};
    using synth::AIChatComponent;
    for (const char* text : {"dark drum and bass", "make a song about rain", "write me a lofi tune", "acid techno",
                             "create an instrumental from scratch", "give me a 174 bpm dnb track",
                             "make me a chill track", "write a 2 minute house tune with drums, bass and chords",
                             "compose a lofi banger", "schreib mir ein Lied", "make a 3-minute disco jam",
                             "I want a slow experimental track", "a dark cinematic piece", "slow experimental track"})
        EXPECT_TRUE(AIChatComponent::shouldPlanSong(juce::String::fromUTF8(text), modules)) << text;
    EXPECT_TRUE(AIChatComponent::shouldPlanSong(juce::String::fromUTF8("genera una canci\xC3\xB3n triste"), modules));
    for (const char* text : {"add a filter to the lead", "make the bass darker", "add a bass line to the song",
                             "change the reverb decay", "remove the delay", "what does the filter cutoff do", "hello",
                             "add a house kick between the snare and the hat", "add a track", "new track with a bass",
                             "make a bassline in A minor", "add a slow track", "make the dark track louder",
                             "change this to F dominant Phrygian"})
        EXPECT_FALSE(AIChatComponent::shouldPlanSong(text, modules)) << text;
}

TEST_F(AIChatComponentTest, ASongRequestShowsAPlanCardAndBuildsNothing) {
    SongRig rig;
    rig.send("dark drum and bass");
    EXPECT_EQ(rig.provider->routes, std::vector<juce::String>{"/v1/song/plan"});
    EXPECT_TRUE(rig.provider->capabilityCalls.empty());
    auto* card = rig.planCard();
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->buildButtonText(), juce::String::fromUTF8("Build song \xC2\xB7 5 uses"));
    EXPECT_EQ(rig.engine.getGraph().getNumNodes(), 0) << "planning changes nothing";
}

TEST_F(AIChatComponentTest, ChangingLengthReplansWithThePreviousPlanAndUpdatesThePrice) {
    SongRig rig;
    rig.provider->planAnswers = {kPlanData, kRePlanData};
    rig.send("dark drum and bass");
    rig.planCard()->chooseForTesting(2, rig.chat->getLastSongSessionForTesting()->detailIndex);
    synth::test::waitUntil([&] { return (rig.provider->planBodies.size()) == (2u); });

    ASSERT_EQ(rig.provider->planBodies.size(), 2u);
    EXPECT_TRUE(rig.provider->planBodies[1]["previousPlan"].isObject());
    EXPECT_EQ(rig.provider->planBodies[1]["length"].toString(), juce::String("long"));
    ASSERT_NE(rig.planCard(), nullptr);
    EXPECT_EQ(rig.planCard()->buildButtonText(), juce::String::fromUTF8("Build song \xC2\xB7 8 uses"));
    EXPECT_EQ(rig.chat->getLastSongSessionForTesting()->lengthIndex, 2) << "a re-plan keeps what the user picked";
    EXPECT_EQ(rig.provider->routes.size(), 2u) << "re-planning is free and builds nothing";
}

TEST_F(AIChatComponentTest, BuildStreamsTheStepsCardThenAppliesTheEnvelopeAsTheSong) {
    SongRig rig;
    rig.send("dark drum and bass");
    rig.planCard()->pressBuildForTesting();
    synth::test::waitUntil([&] { return (rig.provider->routes.back()) == (juce::String("/v1/song/build")); });

    EXPECT_EQ(rig.provider->routes.back(), juce::String("/v1/song/build"));
    EXPECT_EQ(rig.provider->buildBody["token"].toString(), juce::String("tok"));
    auto* steps = rig.buildCard();
    ASSERT_NE(steps, nullptr) << "Build swaps the plan card for the live steps card";

    rig.provider->line(
        R"({"type":"steps","steps":[{"id":"track:Kit","kind":"track","label":"Kit sound"},{"id":"sections","kind":"sections","label":"Laying out"}]})");
    rig.provider->line(R"({"type":"step","id":"track:Kit","status":"running"})");
    synth::test::waitUntil([&] {
        return rig.buildCard() != nullptr && rig.buildCard()->nowText() == "Kit sound" &&
               rig.buildCard()->progressText() == "step 1 of 2";
    });
    steps = rig.buildCard();
    ASSERT_NE(steps, nullptr);
    EXPECT_EQ(steps->nowText(), juce::String("Kit sound"));
    EXPECT_EQ(steps->progressText(), juce::String("step 1 of 2"));
    EXPECT_EQ(rig.engine.getGraph().getNumNodes(), 0) << "nothing lands before the final line";

    rig.provider->line(R"({"type":"step","id":"track:Kit","status":"done","ms":900})");
    rig.provider->line(R"({"type":"step","id":"sections","status":"done"})");
    rig.provider->line(juce::String(R"({"type":"done","envelope":)") + kEnvelope +
                       R"(,"notes":[],"summary":{"tracks":1,"sections":2,"bars":48,"seconds":66}})");
    rig.provider->end();
    synth::test::waitUntil([&] { return (rig.engine.getGraph().getNumNodes()) == (2); });

    EXPECT_EQ(rig.engine.getGraph().getNumNodes(), 2) << "the envelope was applied";
    EXPECT_EQ(rig.chat->getLastSongSessionForTesting()->phase, synth::SongSession::Phase::built);
    ASSERT_NE(rig.buildCard(), nullptr);
    EXPECT_EQ(rig.buildCard()->headerText(), juce::String("Your song is ready"));
    EXPECT_FALSE(rig.chat->hasEditPlanCardForTesting(rig.chat->getMessageCountForTesting() - 1))
        << "the steps card is the only card of a song message";
    EXPECT_TRUE(rig.buildCard()->detailsButtonShownForTesting());
    EXPECT_TRUE(rig.chat->getMessageTextForTesting(rig.chat->getMessageCountForTesting() - 1).startsWith("Built \""))
        << "the bubble text moved on from the plan line to the built line";
    EXPECT_NE(rig.chat->getLastSongSessionForTesting()->detailsText, juce::String());
}

TEST_F(AIChatComponentTest, CancellingABuildClosesTheStreamAndTheCardSaysTheUsesCameBack) {
    SongRig rig;
    rig.send("dark drum and bass");
    rig.planCard()->pressBuildForTesting();
    synth::test::waitUntil([&] { return (rig.buildCard()) != (nullptr); });
    ASSERT_NE(rig.buildCard(), nullptr);

    rig.chat->simulateCancelClick();
    synth::test::waitUntil([&] {
        return rig.buildCard() != nullptr && rig.buildCard()->headerText() == "Build cancelled" &&
               rig.chat->getMessageTextForTesting(rig.chat->getMessageCountForTesting() - 1) ==
                   "The song was not built.";
    });
    EXPECT_EQ(rig.provider->cancelled, std::vector<uint64_t>{9});
    EXPECT_EQ(rig.chat->getMessageTextForTesting(rig.chat->getMessageCountForTesting() - 1),
              juce::String("The song was not built."));
    ASSERT_NE(rig.buildCard(), nullptr);
    EXPECT_EQ(rig.buildCard()->headerText(), juce::String("Build cancelled"));
    EXPECT_EQ(rig.chat->getLastSongSessionForTesting()->phase, synth::SongSession::Phase::failed);
    EXPECT_EQ(rig.engine.getGraph().getNumNodes(), 0);
}

TEST_F(AIChatComponentTest, AnErrorLineEndsTheBuildInTheCardNotInABubble) {
    SongRig rig;
    rig.send("dark drum and bass");
    rig.planCard()->pressBuildForTesting();
    synth::test::waitUntil([&] { return rig.buildCard() != nullptr; });
    const auto messagesBefore = rig.chat->getMessageCountForTesting();
    rig.provider->line(R"({"type":"error","message":"The build failed","refunded":true})");
    rig.provider->end();
    synth::test::waitUntil([&] {
        return rig.buildCard() != nullptr && rig.buildCard()->headerText() == "Build stopped" &&
               rig.buildCard()->nowText() == "The build failed";
    });
    EXPECT_EQ(rig.chat->getMessageCountForTesting(), messagesBefore);
    EXPECT_EQ(rig.buildCard()->headerText(), juce::String("Build stopped"));
    EXPECT_EQ(rig.buildCard()->nowText(), juce::String("The build failed"));
}

namespace {
juce::String lastTurnKind(SongRig& rig) {
    const auto turns = rig.chat->buildChatExportForTesting()["turns"];
    return turns[turns.size() - 1]["kind"].toString();
}
} // namespace

// Regression test for FRO2062: a song asked of a service without the song routes was recorded as an edit or patch.
TEST_F(AIChatComponentTest, ASongRequestStaysASongWhenTheServiceHasNoSongRoutes) {
    SongRig hosted404;
    hosted404.provider->planStatus = 404;
    hosted404.send("dark drum and bass");
    EXPECT_EQ(lastTurnKind(hosted404), "song");

    SongRig local;
    local.provider->hosted = false;
    local.send("write a 2 minute house tune with drums, bass and chords");
    EXPECT_EQ(lastTurnKind(local), "song");
    EXPECT_TRUE(local.provider->routes.empty());
    bool noted = false;
    for (size_t i = 0; i < local.chat->getMessageCountForTesting(); ++i)
        noted = noted || local.chat->getMessageTextForTesting(i).startsWith("Song steps aren't available");
    EXPECT_TRUE(noted);
}

TEST(SongRouting, APlanThatBuildsTwoTracksIsSongShaped) {
    using synth::AIChatComponent;
    const auto plan = [](const char* json) { return juce::JSON::parse(json); };
    EXPECT_TRUE(AIChatComponent::isSongShapedPlan(
        plan(R"({"timelineOps":[{"op":"addInstrumentTrack"},{"op":"addInstrumentTrack"},{"op":"placeClips"}]})")));
    EXPECT_FALSE(AIChatComponent::isSongShapedPlan(plan(R"({"timelineOps":[{"op":"addInstrumentTrack"}]})")));
    EXPECT_FALSE(AIChatComponent::isSongShapedPlan(plan(R"({"nodes":[]})")));
}

TEST_F(AIChatComponentTest, AServiceWithoutTheSongRoutesFallsBackToProjectGenerateWithOneNoteSayingWhy) {
    SongRig rig;
    rig.provider->planStatus = 404;
    rig.send("dark drum and bass");
    EXPECT_EQ(rig.provider->capabilityCalls, std::vector<juce::String>{"project.generate"});
    EXPECT_EQ(rig.planCard(), nullptr);
    bool noted = false;
    for (size_t i = 0; i < rig.chat->getMessageCountForTesting(); ++i)
        noted = noted || rig.chat->getMessageTextForTesting(i) ==
                             "Song steps aren't available on this server, so I'm building it in one go.";
    EXPECT_TRUE(noted);
    EXPECT_FALSE(rig.service.canPlanSongs());
    for (const auto& line : juce::StringArray::fromLines(rig.chat->getLastPlanPreviewForTesting()))
        EXPECT_FALSE(line.contains("Error")) << line;

    rig.send("another song please");
    EXPECT_EQ(rig.provider->routes.size(), 1u) << "no second plan request once the service said 404";
    EXPECT_EQ(rig.provider->capabilityCalls.size(), 2u);
}

TEST_F(AIChatComponentTest, AnEditStaysOnProjectGenerateEvenWithTheSongRoutesAvailable) {
    SongRig rig;
    rig.send("add a filter to the lead");
    EXPECT_TRUE(rig.provider->routes.empty());
    EXPECT_EQ(rig.provider->capabilityCalls, std::vector<juce::String>{"project.generate"});
}

TEST_F(AIChatComponentTest, ATypedChangeWhileAPlanWaitsPlansAgainFromThatPlan) {
    SongRig rig;
    rig.provider->planAnswers = {kPlanData, kRePlanData};
    rig.send("dark drum and bass");
    rig.send("no lead, add a piano");
    ASSERT_EQ(rig.provider->planBodies.size(), 2u);
    EXPECT_TRUE(rig.provider->planBodies[1]["previousPlan"].isObject());
    EXPECT_EQ(rig.provider->planBodies[1]["userPrompt"].toString(), juce::String("no lead, add a piano"));
    EXPECT_TRUE(rig.provider->capabilityCalls.empty());
}

// Regression test for FRO2182: a built song's line wraps in a narrow panel; every line the editor wraps it to must stay
// visible, so the closing quote of Built "<name>". is never clipped away, whatever the panel width.
TEST_F(AIChatComponentTest, TheBuiltLineShowsItsClosingQuoteWhenTheNameWrapsInANarrowPanel) {
    SongRig rig;
    rig.provider->planAnswers.push_back(
        juce::String(kPlanData).replace("Dark drum and bass", "Neon Night Drive in C Minor"));
    rig.send("dark drum and bass");
    rig.planCard()->pressBuildForTesting();
    synth::test::waitUntil([&] { return (rig.provider->routes.back()) == (juce::String("/v1/song/build")); });
    rig.provider->line(juce::String(R"({"type":"done","envelope":)") + kEnvelope +
                       R"(,"notes":[],"summary":{"tracks":1,"sections":2,"bars":48,"seconds":66}})");
    rig.provider->end();
    synth::test::waitUntil(
        [&] { return rig.chat->getLastSongSessionForTesting()->phase == synth::SongSession::Phase::built; });

    const auto expected = juce::String("Built \"Neon Night Drive in C Minor\".");
    synth::test::waitUntil([&] {
        return findDescendantWithText<synth::SelectableChatText>(findMessageList(*rig.chat), expected) != nullptr;
    });
    ASSERT_EQ(rig.chat->getMessageTextForTesting(rig.chat->getMessageCountForTesting() - 1), expected);
    for (int width = 200; width <= 420; width += 3) {
        rig.chat->setSize(width, 700); // lays the list out synchronously
        auto* view = findDescendantWithText<synth::SelectableChatText>(findMessageList(*rig.chat), expected);
        ASSERT_NE(view, nullptr) << "the bubble shows the built line, not the plan line";
        const int wrappedHeight = view->getTextBounds({0, view->getTotalNumChars()}).getBounds().getHeight();
        EXPECT_GE(view->getParentComponent()->getHeight(), wrappedHeight)
            << "panel width " << width << ": the clip is as tall as every line the editor wraps the text to";
    }
}

// Regression test for FRO1051: a Length/Detail click read every installed pack's pack.json once per question the card
// asked (about a dozen scans), freezing the app for tens of milliseconds on a long chat with many packs.
TEST_F(AIChatComponentTest, AChoiceClickScansTheInstalledPacksOnce) {
    using namespace sound_pack_test;
    juce::SharedResourcePointer<synth::soundpacks::SoundPackInstaller> installer;
    TempDir dir;
    synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{dir.path()});
    installer->setInstallRootForTesting(dir.path());
    for (int p = 0; p < 3; ++p) {
        juce::String instruments;
        for (int i = 0; i < 20; ++i)
            instruments << (i ? "," : "") << R"({"id":"i)" << i << R"(","name":"Instrument )" << i
                        << R"(","sfz":"a/b.sfz","category":"Keys"})";
        const auto pack = dir.path().getChildFile("pack" + juce::String(p));
        pack.createDirectory();
        pack.getChildFile("pack.json")
            .replaceWithText(R"({"id":"pack)" + juce::String(p) + R"(","name":"P","version":"1","instruments":[)" +
                             instruments + "]}");
    }
    SongRig rig;
    const juce::String longText = juce::String::repeatedString("lorem ipsum dolor sit amet ", 40);
    for (int i = 0; i < 4; ++i)
        rig.send(longText);
    rig.provider->planAnswers = {
        juce::String(kPlanData).replace("\"sound\":\"x\"", "\"sound\":\"x\",\"coreSound\":\"core/upright-piano\"")};
    rig.send("dark drum and bass");
    auto* card = rig.planCard();
    ASSERT_NE(card, nullptr);
    rig.provider->holdPlans = true;

    const int before = installer->scanCountForTesting();
    card->chooseForTesting(2, rig.chat->getLastSongSessionForTesting()->detailIndex);
    synth::test::waitUntil([&] { return static_cast<bool>(rig.provider->heldPlan); });
    // The stall was the number of folder scans per click, so that count is the guard; a wall-clock bound failed on the
    // slow instrumented Linux runner (1.7 s for a 0.1 s click on a Mac).
    EXPECT_LE(installer->scanCountForTesting() - before, 2) << "one scan per refresh, not one per question";
    synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt);
}

TEST_F(AIChatComponentTest, TheTestModeSeamShowsABuildCardWithThirtySteps) {
    SongRig rig;
    ASSERT_TRUE(rig.chat->showSongCardForTesting(kPlanData, "building", 30, 12));
    synth::test::waitUntil([&] { return rig.buildCard() != nullptr; });
    auto session = rig.chat->getLastSongSessionForTesting();
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->model.total(), 30);
    EXPECT_EQ(session->model.finishedCount(), 12);
    EXPECT_NE(rig.buildCard(), nullptr);
    EXPECT_TRUE(rig.provider->routes.empty()) << "the seam never reaches the network";
}

TEST_F(AIChatComponentTest, TheTestModeSeamShowsAPlanCardAndABuiltCard) {
    SongRig rig;
    ASSERT_TRUE(rig.chat->showSongCardForTesting(kPlanData, "plan", 30, 0));
    synth::test::waitUntil([&] { return rig.planCard() != nullptr; });
    EXPECT_NE(rig.planCard(), nullptr);
    ASSERT_TRUE(rig.chat->showSongCardForTesting(kPlanData, "built", 30, 0));
    synth::test::waitUntil([&] { return rig.buildCard() != nullptr; });
    EXPECT_NE(rig.buildCard(), nullptr);
    EXPECT_TRUE(rig.chat->getLastSongSessionForTesting()->model.isDone());
    EXPECT_FALSE(rig.chat->showSongCardForTesting("not json", "plan", 30, 0));
}

TEST_F(AIChatComponentTest, AFollowUpChipFillsTheChatBoxAndFocusesItWithoutSendingAndTheHintInvitesFollowUps) {
    SongRig rig;
    EXPECT_TRUE(rig.chat->getInputHintForTesting().startsWith("Ask for a sound"));
    ASSERT_TRUE(rig.chat->showSongCardForTesting(kPlanData, "built", 3, 3));
    synth::test::waitUntil([&] { return rig.buildCard() != nullptr; });
    ASSERT_NE(rig.buildCard(), nullptr);
    EXPECT_EQ(rig.chat->getInputHintForTesting(), juce::String("Ask for a change to this song, or ask why"));
    const auto messages = rig.chat->getMessageCountForTesting();
    rig.buildCard()->followUpForTesting(0).onClick();
    auto& input = rig.chat->getInputFieldForTesting();
    EXPECT_EQ(input.getText(), juce::String("Make it darker"));
    EXPECT_EQ(input.getCaretPosition(), input.getTotalNumChars()) << "caret at the end";
    EXPECT_EQ(rig.chat->getInputFocusRequestsForTesting(), 1);
    EXPECT_EQ(rig.chat->getMessageCountForTesting(), messages) << "nothing was sent";
    EXPECT_TRUE(rig.provider->routes.empty());
}

// A message typed after a built song goes the ordinary edit way (never a new song plan).
TEST_F(AIChatComponentTest, AFollowUpTypedAfterABuiltSongIsAnEditRequestAndTheHintGoesBack) {
    SongRig rig;
    ASSERT_TRUE(rig.chat->showSongCardForTesting(kPlanData, "built", 3, 3));
    synth::test::waitUntil([&] { return rig.buildCard() != nullptr; });
    rig.buildCard()->followUpForTesting(0).onClick();
    rig.chat->triggerSend();
    for (const auto& route : rig.provider->routes)
        EXPECT_NE(route, juce::String("/v1/song/plan")) << "a follow-up never plans a new song";
    EXPECT_TRUE(rig.chat->getInputHintForTesting().startsWith("Ask for a sound")) << "the invitation ends";
}
