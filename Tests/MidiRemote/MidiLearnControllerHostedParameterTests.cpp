// MidiLearnControllerHostedParameterTests.cpp -- FRO137
// (docs/control/plugin-card-layout.md#interaction-with-midi-remote-and-automation): a plugin-card
// knob (a hosted plugin's own parameter, not one of the node's RangedAudioParameters) MIDI-Learns
// exactly like a built-in knob. Covers the two seams that resolve a hosted target --
// MidiLearnController::arm() (right-click MIDI Learn) and ::assignControl() (the pick-target
// overlay / panel's control-first assign) -- both of which fall back to
// synth::resolveLaneParameter/captureParamIndexHint, the SAME resolver an automation lane uses, so
// a hosted assignment survives a plugin's parameter set moving under it exactly like a lane does.
// Suite name contains "MidiRemote" per the ship-task --gtest_filter convention.

#include "../StubPluginInstance.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiLearnController.h"
#include "Modules/CardLayout.h"
#include "Modules/FilterModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

#include <chrono>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

using namespace synth;
using namespace synth::midi;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
using synth::test::StubParamTraits;
using synth::test::StubPluginInstance;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

juce::PluginDescription stubDescription() {
    juce::PluginDescription description;
    description.name = "MIDI Learn Plugin";
    description.pluginFormatName = "VST3";
    description.uniqueId = 0xC0DE03;
    description.deprecatedUid = 0xC0DE03;
    description.fileOrIdentifier = "/nonexistent/test/path/MidiLearnPlugin.vst3";
    return description;
}

class MidiLearnControllerHostedParameterTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-midilearncontroller-hosted-tests-" + juce::Uuid().toString());
        root_.deleteRecursively();

        engine_ = std::make_unique<AudioEngine>(AudioEngine::HostMode::Hosted);
        graphEditor_ = std::make_unique<GraphEditor>(*engine_);
        controller_ = std::make_unique<MidiLearnController>(*engine_, *graphEditor_, remoteEngine_, doc_, undo_,
                                                            statusBar_, synth::ControllerProfileStore(root_));
        remoteEngine_.setClock([this] { return fakeNowMs_; });

        // A built-in node, for the "unaffected" side of every comparison below.
        builtInNode_ = engine_->getGraph().addNode(std::make_unique<FilterModule>());

        StubParamTraits toggleTraits;
        toggleTraits.boolean = true;
        std::vector<StubParamSpec> specs = {{"cutoff", "Cutoff", 0.0f, {}, false},
                                            {"bypassFx", "Bypass", 0.0f, toggleTraits, false}};
        backend_.setFactory([specs]() -> std::unique_ptr<StubPluginInstance> {
            return std::make_unique<StubPluginInstance>(2, 2, "MIDI Learn Plugin", 0xC0DE03, "VST3", specs);
        });
        auto* hostedModule = new HostedPluginModule();
        hostedNode_ = engine_->getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(hostedModule));
        hosted_ = hostedModule;
        hosted_->prepareToPlay(kSampleRate, kBlockSize);
        hosted_->loadPlugin(stubDescription(), backend_);
        ASSERT_TRUE(pumpUntil([this] { return hosted_->hasInstance(); }));

        graphEditor_->updateComponents();
    }

    void TearDown() override { root_.deleteRecursively(); }

    void send(const juce::MidiMessage& message) { remoteEngine_.handleMessage(hostSourceKey(), message); }
    void settle() {
        remoteEngine_.drain();
        fakeNowMs_ += 600.0;
        remoteEngine_.drain();
    }

    juce::File root_;
    double fakeNowMs_ = 0.0;
    synth::MidiRemoteProjectDoc doc_;
    AppUndoManager undo_;
    StatusBarComponent statusBar_;
    StubBackend backend_;
    std::unique_ptr<AudioEngine> engine_;
    // Declared after engine_ so it is destroyed first, while the hosted parameters any open gesture
    // points at are still alive (docs/development/testing.md#known-flaky-patterns).
    RemoteEngine remoteEngine_;
    std::unique_ptr<GraphEditor> graphEditor_;
    std::unique_ptr<MidiLearnController> controller_;
    juce::AudioProcessorGraph::Node::Ptr builtInNode_;
    juce::AudioProcessorGraph::Node::Ptr hostedNode_;
    HostedPluginModule* hosted_ = nullptr;
};

} // namespace

// ============================================================================
// arm(): paramIndexHint set for hosted, left at -1 for built-in
// ============================================================================

TEST_F(MidiLearnControllerHostedParameterTest, LearnOnAHostedParameterCapturesItsParamIndexHint) {
    controller_->arm(hostedNode_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();

    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramId, "cutoff");
    EXPECT_GE(doc_.assignments[0].target.parameter.paramIndexHint, 0)
        << "a hosted parameter's index is captured -- exactly what an automation lane captures at creation";
}

TEST_F(MidiLearnControllerHostedParameterTest, LearnOnABuiltInParameterNeverSetsAParamIndexHint) {
    controller_->arm(builtInNode_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();

    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramIndexHint, -1)
        << "a built-in RangedAudioParameter's assignment JSON must stay byte-identical to before FRO137";
}

TEST_F(MidiLearnControllerHostedParameterTest, LearnOnAHostedBooleanParameterIsButtonLike) {
    // buttonLike only affects RemoteEngine's own encoding choice, which this test doesn't observe
    // directly -- proving arm() doesn't crash / mis-set the target for a hosted boolean is the
    // regression this guards (isBoolean() is only reachable on the AudioProcessorParameter base,
    // never dereferenced on a RangedAudioParameter that doesn't exist here).
    controller_->arm(hostedNode_->nodeID, "bypassFx");
    EXPECT_TRUE(controller_->isArmed());
    controller_->cancelArmed();
}

TEST_F(MidiLearnControllerHostedParameterTest, LearnOnAnUnresolvableParamIdStillArmsWithNoIndexHint) {
    // Neither a built-in RangedAudioParameter nor a live hosted parameter -- arm() must not crash,
    // and the eventual assignment (if any control is pressed) simply carries no rescue hint.
    controller_->arm(hostedNode_->nodeID, "doesNotExist");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();

    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramIndexHint, -1);
}

// ============================================================================
// assignControl(): the pick-target overlay / panel's control-first assign accepts a hosted pick
// ============================================================================

TEST_F(MidiLearnControllerHostedParameterTest, AssignControlAcceptsAHostedParameterPick) {
    ControllerProfile profile;
    profile.id = "p1";
    profile.name = "Launchkey";
    profile.input.identifier = hostSourceKey();
    Control control;
    control.id = "knob";
    control.name = "Knob 1";
    control.message.type = MessageType::cc;
    control.message.channel = 1;
    control.message.number = 21;
    profile.controls = {control};
    ASSERT_TRUE(controller_->addProfile(profile));

    const auto status = controller_->assignControl("p1", "knob", PickTarget::parameter(hostedNode_->nodeID, "cutoff"));
    ASSERT_EQ(status, AssignStatus::assigned);

    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramId, "cutoff");
    EXPECT_GE(doc_.assignments[0].target.parameter.paramIndexHint, 0);
}

// ============================================================================
// A card layout change is presentation only -- it never touches a lane or a MIDI assignment
// ============================================================================

TEST_F(MidiLearnControllerHostedParameterTest, RemovingAParamFromTheCardLayoutNeverTouchesItsMidiAssignmentOrLane) {
    controller_->arm(hostedNode_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);
    const juce::String nodeUuid = doc_.assignments[0].target.parameter.nodeUuid;

    synth::TimelineDoc timeline;
    const auto trackId = timeline.addTrack(synth::TrackKind::Midi, "Track 1");
    synth::AutomationLane::RangeSnapshot laneRange;
    laneRange.minValue = 0.0f;
    laneRange.maxValue = 1.0f;
    laneRange.defaultValue = 0.5f;
    const auto laneId = timeline.addLane(trackId, nodeUuid, "cutoff", laneRange);
    ASSERT_TRUE(laneId.isValid());

    // The picker's own "untick" path: a card layout naming "cutoff" as a slot, then one that no
    // longer does (docs/control/plugin-card-layout.md: "A parameter removed from the layout keeps
    // its lanes and assignments -- the layout is presentation, never a binding").
    synth::CardLayout withSlot;
    synth::CardSlot slot;
    slot.paramId = "cutoff";
    withSlot.slots.push_back(slot);
    hosted_->setCardLayoutOverride(withSlot.toVar());

    synth::CardLayout withoutSlot; // "cutoff" removed
    hosted_->setCardLayoutOverride(withoutSlot.toVar());

    EXPECT_EQ(doc_.assignments.size(), 1u) << "the MIDI assignment survives";
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramId, "cutoff");
    ASSERT_NE(timeline.getLane(laneId), nullptr) << "the automation lane survives";
    EXPECT_EQ(timeline.getLane(laneId)->paramId, "cutoff");
}

TEST_F(MidiLearnControllerHostedParameterTest, AssignControlRejectsAParamIdThatResolvesNowhere) {
    ControllerProfile profile;
    profile.id = "p1";
    profile.name = "Launchkey";
    profile.input.identifier = hostSourceKey();
    Control control;
    control.id = "knob";
    control.name = "Knob 1";
    control.message.type = MessageType::cc;
    control.message.channel = 1;
    control.message.number = 21;
    profile.controls = {control};
    ASSERT_TRUE(controller_->addProfile(profile));

    EXPECT_EQ(controller_->assignControl("p1", "knob", PickTarget::parameter(hostedNode_->nodeID, "doesNotExist")),
              AssignStatus::unresolvedTarget);
}
