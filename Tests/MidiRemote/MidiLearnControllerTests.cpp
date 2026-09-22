// MidiLearnController's arm/cancel/bind lifecycle (FRO130, docs/control/midi-remote-ui.md#the-learn-interaction).
// Uses HostMode::Hosted so RemoteEngine::handleMessage has a source to test against without a real
// audio device or MIDI hardware (docs/control/midi-remote.md#the-plugin-build-vst3au-inside-a-host's
// hostSourceKey()), and points ControllerProfileStore at a temp directory -- never the real
// settings folder, so a run never collides with a concurrent test suite or a developer's own
// profiles. Same send/drain/advance/drain idiom as RemoteEngineLearnTests.cpp (noteLearnCandidate
// only runs inside drain()'s FIFO-drain loop, never synchronously from handleMessage()). Suite name
// contains "MidiRemote" per the ship-task --gtest_filter convention.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiLearnController.h"
#include "Modules/FilterModule.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

using namespace synth::midi;

namespace {

class MidiLearnControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-midilearncontroller-tests-" + juce::Uuid().toString());
        root_.deleteRecursively();

        engine_ = std::make_unique<AudioEngine>(AudioEngine::HostMode::Hosted);
        graphEditor_ = std::make_unique<GraphEditor>(*engine_);
        controller_ = std::make_unique<MidiLearnController>(*engine_, *graphEditor_, remoteEngine_, doc_, undo_,
                                                            statusBar_, synth::ControllerProfileStore(root_));
        remoteEngine_.setClock([this] { return fakeNowMs_; });

        node_ = engine_->getGraph().addNode(std::make_unique<FilterModule>());
        graphEditor_->updateComponents();
    }

    void TearDown() override { root_.deleteRecursively(); }

    void send(const juce::MidiMessage& message) { remoteEngine_.handleMessage(hostSourceKey(), message); }

    // Mirrors RemoteEngineLearnTests.cpp: one drain to tally + stamp the first-event time, then
    // advance past the settle window and drain again to resolve.
    void settle() {
        remoteEngine_.drain();
        fakeNowMs_ += kLearnSettleMs + 1.0;
        remoteEngine_.drain();
    }

    juce::File root_;
    double fakeNowMs_ = 0.0;
    RemoteEngine remoteEngine_;
    synth::MidiRemoteProjectDoc doc_;
    AppUndoManager undo_;
    StatusBarComponent statusBar_;
    std::unique_ptr<AudioEngine> engine_;
    std::unique_ptr<GraphEditor> graphEditor_;
    std::unique_ptr<MidiLearnController> controller_;
    juce::AudioProcessorGraph::Node::Ptr node_;
};

} // namespace

TEST_F(MidiLearnControllerTest, ArmMakesTheEngineArmedAndCancelClearsIt) {
    controller_->arm(node_->nodeID, "cutoff");
    EXPECT_TRUE(controller_->isArmed());

    controller_->cancelArmed();
    EXPECT_FALSE(controller_->isArmed());
    EXPECT_EQ(statusBar_.getTransientMessageForTest(), "MIDI Learn cancelled");
}

TEST_F(MidiLearnControllerTest, CancelArmedWithNothingArmedIsANoOp) {
    controller_->cancelArmed();
    EXPECT_TRUE(statusBar_.getTransientMessageForTest().isEmpty());
}

TEST_F(MidiLearnControllerTest, LearnCreatesAnAssignmentAndAnAutoProfile) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();

    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].spec.number, 20);
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramId, "cutoff");
    EXPECT_FALSE(doc_.assignments[0].target.parameter.nodeUuid.isEmpty());

    ASSERT_EQ(controller_->getProfiles().size(), 1u);
    EXPECT_EQ(controller_->getProfiles()[0].controls.size(), 1u) << "an unknown source auto-creates a profile+control";
    ASSERT_EQ(doc_.controllers.size(), 1u);

    EXPECT_FALSE(controller_->isArmed()) << "a successful bind ends the armed UI state";
    EXPECT_EQ(statusBar_.getTransientMessageForTest(), "Mapped to CC 20 on Host MIDI");
}

TEST_F(MidiLearnControllerTest, LearnAgainReplacesTheExistingAssignmentRatherThanDuplicatingIt) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);

    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 30, 64));
    settle();

    ASSERT_EQ(doc_.assignments.size(), 1u) << "learn again replaces, never duplicates";
    EXPECT_EQ(doc_.assignments[0].spec.number, 30);
}

TEST_F(MidiLearnControllerTest, ForgetRemovesTheAssignment) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);

    controller_->forget(node_->nodeID, "cutoff");
    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_EQ(statusBar_.getTransientMessageForTest(), "MIDI mapping removed");
}

TEST_F(MidiLearnControllerTest, ForgetOnAnUnmappedTargetIsANoOp) {
    controller_->forget(node_->nodeID, "cutoff");
    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_TRUE(statusBar_.getTransientMessageForTest().isEmpty());
}

TEST_F(MidiLearnControllerTest, QueryMappingsReturnsALabelForAMappedParamOnlyForItsOwnNode) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();

    const auto mappings = controller_->queryMappings(node_->nodeID);
    ASSERT_EQ(mappings.count("cutoff"), 1u);
    EXPECT_EQ(mappings.at("cutoff"), "CC 20 on Host MIDI");
    EXPECT_EQ(mappings.count("resonance"), 0u);

    const juce::AudioProcessorGraph::NodeID unrelatedNodeId(999);
    EXPECT_TRUE(controller_->queryMappings(unrelatedNodeId).empty());
}

TEST_F(MidiLearnControllerTest, LearnIsUndoableButTheAutoCreatedProfileStays) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);

    ASSERT_TRUE(undo_.canUndo());
    undo_.undo();

    EXPECT_TRUE(doc_.assignments.empty());
    // docs/control/midi-remote.md#undo: "the profile stays" -- only the project-doc half is undone.
    EXPECT_EQ(controller_->getProfiles().size(), 1u);
}

TEST_F(MidiLearnControllerTest, ArmingAgainOnADifferentControlTearsDownThePreviousArmedUi) {
    controller_->arm(node_->nodeID, "cutoff");
    ASSERT_TRUE(controller_->isArmed());

    controller_->arm(node_->nodeID, "resonance");
    EXPECT_TRUE(controller_->isArmed());

    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramId, "resonance");
}
