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
#include "Modules/ChannelStripModule.h"
#include "Modules/FilterModule.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

using namespace synth;
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

    void TearDown() override {
        // remoteEngine_ outlives engine_ (declared first): end any open gesture while its parameter
        // still exists -- RemoteEngine::endAllGestures()'s lifetime contract.
        remoteEngine_.endAllGestures();
        root_.deleteRecursively();
    }

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

// Mirrors MainComponent::RemoteActionInvokerImpl::invokeNodeCommand's body exactly, same as
// RemoteEngineNodeCommandE2ETests.cpp's ToggleSoloInvoker -- that real type is a private nested
// type of MainComponent, out of reach for this headless suite.
class ToggleSoloInvoker : public RemoteActionInvoker {
public:
    ToggleSoloInvoker(AudioEngine& engine, AppUndoManager& undo)
        : engine_(engine)
        , undo_(undo) {}

    void invokeRemoteCommand(juce::CommandID) override {}

    // FRO236: this suite doesn't exercise continuous targets -- stub, never called.
    double getContinuousValue(ContinuousTargetKind) override { return 0.0; }
    void setContinuousValue(ContinuousTargetKind, double) override {}
    bool getContinuousWindow(ContinuousTargetKind, double&, double&) override { return false; }

    void invokeNodeCommand(juce::AudioProcessorGraph::NodeID nodeId, NodeCommandKind command) override {
        if (command != NodeCommandKind::toggleSolo)
            return;
        auto& graph = engine_.getGraph();
        auto* node = graph.getNodeForId(nodeId);
        auto* strip = node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
        if (strip == nullptr)
            return;
        undo_.captureBeforeState(graph);
        engine_.setChannelStripSoloed(nodeId, !strip->isSoloed());
        undo_.pushSnapshotFromCapture(graph);
    }

private:
    AudioEngine& engine_;
    AppUndoManager& undo_;
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

// ============================================================================
// FRO133: action targets (docs/control/midi-remote.md#action-targets) -- armAction()/
// forgetAction()/queryActionMappings() mirror arm()/forget()/queryMappings() above, but write the
// assignment into the learned device's ControllerProfile.actions (GLOBAL) rather than doc_
// (project), and are NOT undoable (docs/control/midi-remote.md#undo: "Profile edits ... not
// undoable").
// ============================================================================

TEST_F(MidiLearnControllerTest, ArmActionMakesTheEngineArmed) {
    controller_->armAction("transportRecord");
    EXPECT_TRUE(controller_->isArmed());

    controller_->cancelArmed();
    EXPECT_FALSE(controller_->isArmed());
}

TEST_F(MidiLearnControllerTest, LearnActionCreatesAGlobalProfileAssignmentNeverAProjectOne) {
    controller_->armAction("transportRecord");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();

    EXPECT_TRUE(doc_.assignments.empty()) << "an action assignment never touches the project doc";
    EXPECT_TRUE(doc_.controllers.empty());

    ASSERT_EQ(controller_->getProfiles().size(), 1u);
    const auto& actions = controller_->getProfiles()[0].actions;
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_TRUE(actions[0].target.isAction());
    EXPECT_EQ(actions[0].target.action.actionId, "transportRecord");
    EXPECT_EQ(actions[0].spec.number, 20);

    EXPECT_FALSE(controller_->isArmed());
    EXPECT_EQ(statusBar_.getTransientMessageForTest(), "Mapped to CC 20 on Host MIDI");
}

TEST_F(MidiLearnControllerTest, LearnActionAgainReplacesRatherThanDuplicating) {
    controller_->armAction("transportRecord");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(controller_->getProfiles()[0].actions.size(), 1u);

    controller_->armAction("transportRecord");
    send(juce::MidiMessage::controllerEvent(1, 30, 64));
    settle();

    const auto& actions = controller_->getProfiles()[0].actions;
    ASSERT_EQ(actions.size(), 1u) << "learn again replaces, never duplicates";
    EXPECT_EQ(actions[0].spec.number, 30);
}

TEST_F(MidiLearnControllerTest, LearnActionIsNotUndoable) {
    controller_->armAction("transportRecord");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(controller_->getProfiles()[0].actions.size(), 1u);

    EXPECT_FALSE(undo_.canUndo()) << "a profile edit is a global setting, not a project-doc undo step";
}

TEST_F(MidiLearnControllerTest, ForgetActionRemovesTheAssignmentFromTheProfile) {
    controller_->armAction("transportRecord");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(controller_->getProfiles()[0].actions.size(), 1u);

    controller_->forgetAction("transportRecord");
    EXPECT_TRUE(controller_->getProfiles()[0].actions.empty());
    EXPECT_EQ(statusBar_.getTransientMessageForTest(), "MIDI mapping removed");
}

TEST_F(MidiLearnControllerTest, ForgetActionOnAnUnmappedActionIsANoOp) {
    controller_->forgetAction("transportRecord");
    EXPECT_TRUE(statusBar_.getTransientMessageForTest().isEmpty());
}

TEST_F(MidiLearnControllerTest, QueryActionMappingsReturnsALabelForEachMappedAction) {
    controller_->armAction("transportRecord");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();

    const auto mappings = controller_->queryActionMappings();
    ASSERT_EQ(mappings.count("transportRecord"), 1u);
    EXPECT_EQ(mappings.at("transportRecord"), "CC 20 on Host MIDI");
    EXPECT_EQ(mappings.count("transportToggleLoop"), 0u);
}

// ============================================================================
// FRO253: node command targets (docs/control/midi-remote.md#node-command-targets) --
// armNodeCommand()/forgetNodeCommand()/queryNodeCommandMappings() mirror arm()/forget()/
// queryMappings() above (PROJECT-scoped, undoable, "learn again" replaces), unlike the action
// overloads (GLOBAL, on the controller edit history instead).
// ============================================================================

TEST_F(MidiLearnControllerTest, ArmNodeCommandMakesTheEngineArmed) {
    controller_->armNodeCommand(node_->nodeID, synth::NodeCommandKind::toggleSolo);
    EXPECT_TRUE(controller_->isArmed());

    controller_->cancelArmed();
    EXPECT_FALSE(controller_->isArmed());
}

TEST_F(MidiLearnControllerTest, LearnNodeCommandCreatesAProjectDocAssignment) {
    controller_->armNodeCommand(node_->nodeID, synth::NodeCommandKind::toggleSolo);
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();

    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_TRUE(doc_.assignments[0].target.isNodeCommand());
    EXPECT_EQ(doc_.assignments[0].target.nodeCommand.command, synth::NodeCommandKind::toggleSolo);
    EXPECT_FALSE(doc_.assignments[0].target.nodeCommand.nodeUuid.isEmpty());

    EXPECT_FALSE(controller_->isArmed());
}

TEST_F(MidiLearnControllerTest, LearnNodeCommandAgainReplacesRatherThanDuplicating) {
    controller_->armNodeCommand(node_->nodeID, synth::NodeCommandKind::toggleSolo);
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);

    controller_->armNodeCommand(node_->nodeID, synth::NodeCommandKind::toggleSolo);
    send(juce::MidiMessage::controllerEvent(1, 30, 64));
    settle();

    ASSERT_EQ(doc_.assignments.size(), 1u) << "learn again replaces, never duplicates";
    EXPECT_EQ(doc_.assignments[0].spec.number, 30);
}

TEST_F(MidiLearnControllerTest, LearnNodeCommandIsUndoable) {
    controller_->armNodeCommand(node_->nodeID, synth::NodeCommandKind::toggleSolo);
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);

    ASSERT_TRUE(undo_.canUndo());
    undo_.undo();
    EXPECT_TRUE(doc_.assignments.empty());
}

TEST_F(MidiLearnControllerTest, ForgetNodeCommandRemovesTheAssignment) {
    controller_->armNodeCommand(node_->nodeID, synth::NodeCommandKind::toggleSolo);
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);

    controller_->forgetNodeCommand(node_->nodeID, synth::NodeCommandKind::toggleSolo);
    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_EQ(statusBar_.getTransientMessageForTest(), "MIDI mapping removed");
}

TEST_F(MidiLearnControllerTest, ForgetNodeCommandOnAnUnmappedTargetIsANoOp) {
    controller_->forgetNodeCommand(node_->nodeID, synth::NodeCommandKind::toggleSolo);
    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_TRUE(statusBar_.getTransientMessageForTest().isEmpty());
}

TEST_F(MidiLearnControllerTest, QueryNodeCommandMappingsReportsTheLabelForItsOwnNode) {
    controller_->armNodeCommand(node_->nodeID, synth::NodeCommandKind::toggleSolo);
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();

    const auto mappings = controller_->queryNodeCommandMappings(node_->nodeID);
    ASSERT_EQ(mappings.count(synth::NodeCommandKind::toggleSolo), 1u);
    EXPECT_EQ(mappings.at(synth::NodeCommandKind::toggleSolo), "CC 20 on Host MIDI");

    const juce::AudioProcessorGraph::NodeID unrelatedNodeId(999);
    EXPECT_TRUE(controller_->queryNodeCommandMappings(unrelatedNodeId).empty());
}

TEST_F(MidiLearnControllerTest, ArmNodeCommandTearsDownAPreviouslyArmedParameterLearnAndViceVersa) {
    controller_->arm(node_->nodeID, "cutoff");
    ASSERT_TRUE(controller_->isArmed());

    controller_->armNodeCommand(node_->nodeID, synth::NodeCommandKind::toggleSolo);
    EXPECT_TRUE(controller_->isArmed());
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();

    EXPECT_TRUE(doc_.assignments.size() == 1u && doc_.assignments[0].target.isNodeCommand())
        << "only the node command learn should have settled";
}

TEST_F(MidiLearnControllerTest, ArmActionTearsDownAPreviouslyArmedParameterLearnAndViceVersa) {
    controller_->arm(node_->nodeID, "cutoff");
    ASSERT_TRUE(controller_->isArmed());

    controller_->armAction("transportRecord");
    EXPECT_TRUE(controller_->isArmed());
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();

    EXPECT_TRUE(doc_.assignments.empty()) << "only the action learn should have settled";
    ASSERT_EQ(controller_->getProfiles()[0].actions.size(), 1u);

    controller_->arm(node_->nodeID, "resonance");
    EXPECT_TRUE(controller_->isArmed());
    send(juce::MidiMessage::controllerEvent(1, 30, 64));
    settle();

    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramId, "resonance");
}

// ============================================================================
// FRO253 regression: publishAssignments() must re-resolve against the live graph itself.
// RemoteEngine::setAssignments() rebuilds its snapshot with graph == nullptr by design, which only
// carries forward each assignment id's PREVIOUS resolution -- a brand-new id (a just-settled
// learn, or its undo/redo) has none, so without publishAssignments() also reconciling, the target
// stays unresolved until some unrelated graph change happens to reach MainComponent's reconcile
// funnel. These drive real MIDI through the same handleMessage()/drain() path as every test above,
// immediately after settle(), and deliberately never call remoteEngine_.reconcile() themselves --
// the fix must be publishAssignments() doing it internally.
// ============================================================================

TEST_F(MidiLearnControllerTest, LearnThenImmediateCcDrivesTheParameterWithoutAnExplicitReconcile) {
    // Takeover::jump, not the pickup default -- pickup's first hardware move after a fresh learn
    // deliberately doesn't apply (it only arms the crossing check), which would be a false
    // negative here; this test is about resolution, not takeover semantics.
    remoteEngine_.setDefaultTakeover(synth::Takeover::jump);

    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 0));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);

    juce::RangedAudioParameter* cutoff = nullptr;
    for (auto* p : node_->getProcessor()->getParameters()) {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
            ranged != nullptr && ranged->paramID == "cutoff") {
            cutoff = ranged;
            break;
        }
    }
    ASSERT_NE(cutoff, nullptr);
    const float before = cutoff->getValue();

    // Same control, a fresh value -- an ordinary hardware move right after the learn settled, with
    // no explicit reconcile() call in between.
    send(juce::MidiMessage::controllerEvent(1, 20, 127));
    remoteEngine_.drain();

    EXPECT_NE(cutoff->getValue(), before)
        << "the freshly learned assignment must already be resolved against the live graph";

    // Same idle-gesture end as RemoteEngineApplyTests.cpp -- close the still-open change gesture
    // before the fixture tears down the graph, or ~RemoteEngine's endAllGestures() dereferences a
    // now-dangling juce::AudioParameterFloat*.
    fakeNowMs_ += kGestureIdleMs + 1.0;
    remoteEngine_.drain();
}

TEST_F(MidiLearnControllerTest, LearnNodeCommandThenImmediatePressTogglesSoloWithoutAnExplicitReconcile) {
    auto stripNode = engine_->getGraph().addNode(std::make_unique<ChannelStripModule>());
    auto* strip = dynamic_cast<ChannelStripModule*>(stripNode->getProcessor());
    ASSERT_NE(strip, nullptr);
    graphEditor_->updateComponents();

    ToggleSoloInvoker invoker(*engine_, undo_);
    remoteEngine_.setActionInvoker(&invoker);

    controller_->armNodeCommand(stripNode->nodeID, synth::NodeCommandKind::toggleSolo);
    send(juce::MidiMessage::controllerEvent(1, 20, 127));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);
    ASSERT_FALSE(strip->isSoloed());

    // A second press right after the learn settled, no explicit reconcile() call in between.
    send(juce::MidiMessage::controllerEvent(1, 20, 127));
    remoteEngine_.drain();

    EXPECT_TRUE(strip->isSoloed())
        << "the freshly learned node-command assignment must already be resolved against the live graph";
}

// ============================================================================
// FRO131: MIDI Remote panel profile mutations (updateProfile, countProjectAssignmentsForProfile,
// deleteProfile, deleteControl, updateAssignment) -- FRO130 arm/forget/learn paths handle
// the project doc half (project history) and auto-profiles (controller edit history);
// these five panel-side methods route profile edits and project-doc removals through one
// seam so the engine's published snapshot never goes stale.
// ============================================================================

TEST_F(MidiLearnControllerTest, UpdateProfileRenamesTheControllerAndReflectsInGetProfiles) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(controller_->getProfiles().size(), 1u);
    const juce::String profileId = controller_->getProfiles()[0].id;
    const juce::String oldName = controller_->getProfiles()[0].name;
    EXPECT_FALSE(oldName.isEmpty());

    ControllerProfile updatedProfile = controller_->getProfiles()[0];
    updatedProfile.name = "My Custom Device";
    EXPECT_TRUE(controller_->updateProfile(updatedProfile));

    ASSERT_EQ(controller_->getProfiles().size(), 1u);
    EXPECT_EQ(controller_->getProfiles()[0].name, "My Custom Device");
    EXPECT_EQ(controller_->getProfiles()[0].id, profileId);
}

TEST_F(MidiLearnControllerTest, UpdateProfileIsNotUndoable) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_TRUE(undo_.canUndo()) << "the parameter learn creates an undo step";
    undo_.undo();
    undo_.redo();
    EXPECT_TRUE(undo_.canUndo());

    ControllerProfile updatedProfile = controller_->getProfiles()[0];
    updatedProfile.name = "New Name";
    controller_->updateProfile(updatedProfile);

    EXPECT_TRUE(undo_.canUndo()) << "a profile edit is a global setting, not a project-doc undo step";
    // Undoing gets us back to the learned state, not before the rename.
    undo_.undo();
    EXPECT_EQ(controller_->getProfiles()[0].name, "New Name") << "renaming the profile did not create an undo step";
}

TEST_F(MidiLearnControllerTest, UpdateProfileReturnsFalseForUnknownProfileId) {
    ControllerProfile unknownProfile;
    unknownProfile.id = "unknown-profile-id";
    unknownProfile.name = "Unknown";
    EXPECT_FALSE(controller_->updateProfile(unknownProfile));
}

TEST_F(MidiLearnControllerTest, CountProjectAssignmentsForProfileCountsMatchingAssignments) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);
    const juce::String profileA = controller_->getProfiles()[0].id;

    // Create a second assignment on the same profile
    controller_->arm(node_->nodeID, "resonance");
    send(juce::MidiMessage::controllerEvent(1, 30, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 2u);
    EXPECT_EQ(doc_.assignments[0].control.profileId, profileA);
    EXPECT_EQ(doc_.assignments[1].control.profileId, profileA);

    EXPECT_EQ(controller_->countProjectAssignmentsForProfile(profileA), 2);

    // An action-target learn on profileA's own device doesn't add a project assignment.
    controller_->armAction("transportRecord");
    send(juce::MidiMessage::controllerEvent(1, 40, 64));
    settle();
    ASSERT_EQ(controller_->getProfiles().size(), 1u) << "same device -- still one profile";
    EXPECT_EQ(controller_->countProjectAssignmentsForProfile(profileA), 2)
        << "action assignments don't count, only project doc (parameter/nodeCommand) assignments";

    // A profile id this fixture never created must count zero -- RemoteEngine::handleMessage
    // drops any message whose sourceKey isn't already registered (refreshSources() only ever
    // knows hostSourceKey() in this Hosted-mode fixture), so a genuine second device/profile isn't
    // reachable here; this still catches a scoping bug (e.g. forgetting to filter by profileId at
    // all, which would wrongly return doc_.assignments.size() for any id).
    EXPECT_EQ(controller_->countProjectAssignmentsForProfile("unknown-id"), 0);
}

TEST_F(MidiLearnControllerTest, DeleteProfileRemovesItFromGetProfilesAndDisk) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(controller_->getProfiles().size(), 1u);
    const juce::String profileId = controller_->getProfiles()[0].id;

    // Verify the profile file exists on disk -- a fresh store against the same root_, since
    // controller_ owns its own ControllerProfileStore instance rather than exposing it.
    auto profiles = synth::ControllerProfileStore(root_).loadAll();
    ASSERT_EQ(profiles.profiles.size(), 1u);

    EXPECT_TRUE(controller_->deleteProfile(profileId));

    EXPECT_TRUE(controller_->getProfiles().empty()) << "the profile is removed from getProfiles()";

    // Verify the profile file is deleted from disk
    profiles = synth::ControllerProfileStore(root_).loadAll();
    EXPECT_TRUE(profiles.profiles.empty()) << "the profile file is deleted";
}

TEST_F(MidiLearnControllerTest, DeleteProfileLeavesProjectAssignmentsUntouched) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);
    const juce::String profileId = controller_->getProfiles()[0].id;

    controller_->deleteProfile(profileId);

    EXPECT_EQ(doc_.assignments.size(), 1u) << "project assignments remain after profile deletion";
    EXPECT_EQ(doc_.assignments[0].control.profileId, profileId)
        << "the assignment still references the deleted profile (orphan state)";
}

TEST_F(MidiLearnControllerTest, DeleteProfileReturnsFalseForUnknownProfileId) {
    EXPECT_FALSE(controller_->deleteProfile("unknown-profile-id"));
}

TEST_F(MidiLearnControllerTest, DeleteControlRemovesItFromProfileAndProjectAssignments) {
    // Set up a profile with 2 controls
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(controller_->getProfiles()[0].controls.size(), 1u);
    const juce::String profileId = controller_->getProfiles()[0].id;
    const juce::String controlId1 = controller_->getProfiles()[0].controls[0].id;

    // Add a second control
    controller_->arm(node_->nodeID, "resonance");
    send(juce::MidiMessage::controllerEvent(1, 30, 64));
    settle();
    ASSERT_EQ(controller_->getProfiles()[0].controls.size(), 2u);
    const juce::String controlId2 = controller_->getProfiles()[0].controls[1].id;

    // Verify we have 2 assignments
    ASSERT_EQ(doc_.assignments.size(), 2u);

    // Delete the first control
    EXPECT_TRUE(controller_->deleteControl(profileId, controlId1));

    // Verify the first control is removed from the profile
    ASSERT_EQ(controller_->getProfiles()[0].controls.size(), 1u);
    EXPECT_EQ(controller_->getProfiles()[0].controls[0].id, controlId2) << "the second control remains";

    // Verify the first assignment is removed from the project
    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramId, "resonance") << "only the resonance assignment remains";
}

TEST_F(MidiLearnControllerTest, DeleteControlRecordsAnUndoStepForProjectAssignmentRemoval) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);
    const auto assignmentId = doc_.assignments[0].id;
    const juce::String profileId = controller_->getProfiles()[0].id;
    const juce::String controlId = controller_->getProfiles()[0].controls[0].id;

    // Prove the learn's own undo step really works, then wipe it -- redo() replays the step, it
    // does not remove it from history, so canUndo() would stay true without clearUndoHistory().
    undo_.undo();
    EXPECT_TRUE(doc_.assignments.empty());
    undo_.redo();
    ASSERT_EQ(doc_.assignments.size(), 1u);
    undo_.clearUndoHistory();
    EXPECT_FALSE(undo_.canUndo());

    // Delete the control
    controller_->deleteControl(profileId, controlId);

    EXPECT_TRUE(doc_.assignments.empty());
    EXPECT_TRUE(undo_.canUndo()) << "the project assignment removal is undoable";
    undo_.undo();
    EXPECT_EQ(doc_.assignments.size(), 1u) << "undoing restores the assignment";
    EXPECT_EQ(doc_.assignments[0].id, assignmentId);
}

TEST_F(MidiLearnControllerTest, DeleteControlRemovesGlobalActionAssignmentOnThatControl) {
    // Create a profile with a control + a global action assignment on it
    controller_->armAction("transportRecord");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(controller_->getProfiles()[0].controls.size(), 1u);
    ASSERT_EQ(controller_->getProfiles()[0].actions.size(), 1u);
    const juce::String profileId = controller_->getProfiles()[0].id;
    const juce::String controlId = controller_->getProfiles()[0].controls[0].id;

    EXPECT_TRUE(controller_->deleteControl(profileId, controlId));

    EXPECT_TRUE(controller_->getProfiles()[0].controls.empty());
    EXPECT_TRUE(controller_->getProfiles()[0].actions.empty())
        << "the action assignment on that control is dropped (part of the profile edit)";
}

TEST_F(MidiLearnControllerTest, DeleteControlReturnsFalseForUnknownProfileOrControl) {
    EXPECT_FALSE(controller_->deleteControl("unknown-profile", "unknown-control"));

    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    const juce::String profileId = controller_->getProfiles()[0].id;

    EXPECT_FALSE(controller_->deleteControl(profileId, "unknown-control"));
    EXPECT_FALSE(controller_->deleteControl("unknown-profile", profileId));
}

TEST_F(MidiLearnControllerTest, UpdateAssignmentChangesExistingProjectAssignmentAndRecordsUndo) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);
    const auto originalId = doc_.assignments[0].id;
    EXPECT_EQ(doc_.assignments[0].takeover, synth::Takeover::useDefault);

    // Clear undo state -- redo() replays the learn's own step, it does not remove it from
    // history, so canUndo() would stay true without an explicit clearUndoHistory().
    undo_.clearUndoHistory();
    EXPECT_FALSE(undo_.canUndo());

    // Update the assignment
    synth::Assignment updated = doc_.assignments[0];
    updated.takeover = synth::Takeover::scale;
    updated.range.min = 0.2f;
    updated.range.max = 0.8f;

    EXPECT_TRUE(controller_->updateAssignment(updated));

    EXPECT_EQ(doc_.assignments[0].id, originalId);
    EXPECT_EQ(doc_.assignments[0].takeover, synth::Takeover::scale);
    EXPECT_EQ(doc_.assignments[0].range.min, 0.2f);
    EXPECT_EQ(doc_.assignments[0].range.max, 0.8f);

    EXPECT_TRUE(undo_.canUndo()) << "the assignment update is undoable";
    undo_.undo();
    EXPECT_EQ(doc_.assignments[0].takeover, synth::Takeover::useDefault) << "undoing restores the original takeover";
    EXPECT_EQ(doc_.assignments[0].range.min, 0.0f);
    EXPECT_EQ(doc_.assignments[0].range.max, 1.0f);
}

TEST_F(MidiLearnControllerTest, UpdateAssignmentReturnsFalseForNonExistentAssignmentId) {
    synth::Assignment unknown;
    unknown.id = "non-existent-id";
    unknown.target.kind = synth::Target::Kind::parameter;
    EXPECT_FALSE(controller_->updateAssignment(unknown));
}

TEST_F(MidiLearnControllerTest, UpdateAssignmentReturnsFalseForActionTargetAssignment) {
    controller_->armAction("transportRecord");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(controller_->getProfiles()[0].actions.size(), 1u);

    // Try to update via the project doc (which doesn't have it)
    synth::Assignment actionAssignment = controller_->getProfiles()[0].actions[0];
    EXPECT_TRUE(actionAssignment.target.isAction());

    EXPECT_FALSE(controller_->updateAssignment(actionAssignment))
        << "updateAssignment rejects action targets (profile edits are not this method's job)";
}

// ============================================================================
// FRO263 (docs/control/midi-remote-ui.md#the-midi-remote-panel): onChanged fires after every
// mutation that changes what the panel shows, including as the undo/redo postRestore -- these
// don't re-check every mutation MidiLearnControllerTests.cpp already covers above, just that the
// hook reaches every DISTINCT code path that republishes profiles_/doc_.assignments (a project
// mutation via publishAssignments(), and the five profile-only paths that call
// remoteEngine_.setProfiles() without it).
// ============================================================================

TEST_F(MidiLearnControllerTest, OnChangedFiresOnLearn) {
    int calls = 0;
    controller_->onChanged = [&] { ++calls; };

    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();

    EXPECT_GE(calls, 1) << "a project assignment (Learn) goes through publishAssignments()";
}

TEST_F(MidiLearnControllerTest, OnChangedFiresOnForgetAndOnItsUndoRedo) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);

    int calls = 0;
    controller_->onChanged = [&] { ++calls; };

    controller_->forget(node_->nodeID, "cutoff");
    EXPECT_GE(calls, 1) << "forget() goes through publishAssignments()";

    calls = 0;
    undo_.undo(); // the ticket's own repro: Forget, then Cmd+Z
    EXPECT_GE(calls, 1) << "undo's postRestore is publishAssignments()";

    calls = 0;
    undo_.redo();
    EXPECT_GE(calls, 1) << "redo's postRestore is publishAssignments() too";
}

TEST_F(MidiLearnControllerTest, OnChangedFiresOnEveryProfileOnlyMutation) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(controller_->getProfiles().size(), 1u);
    auto profile = controller_->getProfiles()[0];
    const juce::String profileId = profile.id;

    int calls = 0;
    controller_->onChanged = [&] { ++calls; };

    // updateProfile() -- e.g. a panel-side rename or a drag-to-reposition.
    profile.name = "Renamed";
    EXPECT_TRUE(controller_->updateProfile(profile));
    EXPECT_EQ(calls, 1);

    // forgetAction() -- an action target has no project assignment, so it never reaches
    // publishAssignments() at all; this is the path that would otherwise be missed.
    calls = 0;
    controller_->armAction("transportRecord");
    send(juce::MidiMessage::controllerEvent(1, 21, 64));
    settle();
    calls = 0; // ignore the Learn's own notify, isolate forgetAction()'s
    controller_->forgetAction("transportRecord");
    EXPECT_EQ(calls, 1);

    // deleteControl() when the control had no project assignment -- only the profile half runs.
    calls = 0;
    controller_->arm(node_->nodeID, "resonance");
    send(juce::MidiMessage::controllerEvent(1, 22, 64));
    settle();
    controller_->forget(node_->nodeID, "resonance"); // drop the project half, keep the control
    const juce::String secondControlId = controller_->getProfiles()[0].controls.back().id;
    calls = 0;
    EXPECT_TRUE(controller_->deleteControl(profileId, secondControlId));
    EXPECT_EQ(calls, 1);

    // deleteProfile().
    calls = 0;
    EXPECT_TRUE(controller_->deleteProfile(profileId));
    EXPECT_EQ(calls, 1);
}

// ============================================================================
// FRO134: add / import / updateControl
// ============================================================================

TEST_F(MidiLearnControllerTest, AddProfileSavesPublishesAndRefusesDuplicatesAndEmptyIds) {
    ControllerProfile profile;
    profile.id = "new-profile";
    profile.name = "Fresh";
    profile.input.identifier = hostSourceKey();
    int changed = 0;
    controller_->onChanged = [&] { ++changed; };

    EXPECT_TRUE(controller_->addProfile(profile));
    EXPECT_EQ(changed, 1);
    ASSERT_EQ(controller_->getProfiles().size(), 1u);
    EXPECT_EQ(synth::ControllerProfileStore(root_).loadAll().profiles.size(), 1u);

    EXPECT_FALSE(controller_->addProfile(profile)) << "same id";
    ControllerProfile noId;
    EXPECT_FALSE(controller_->addProfile(noId));
    EXPECT_EQ(changed, 1);
}

TEST_F(MidiLearnControllerTest, ImportProfileReportsConflictUntilReplaceIsAsked) {
    ControllerProfile profile;
    profile.id = "p1";
    profile.name = "Original";
    ASSERT_TRUE(controller_->addProfile(profile));
    const auto file = root_.getChildFile("doc.json");
    ASSERT_TRUE(controller_->exportProfile("p1", file));

    profile.name = "Changed";
    ASSERT_TRUE(controller_->updateProfile(profile));

    auto conflict = controller_->importProfile(file, false);
    EXPECT_EQ(conflict.status, MidiLearnController::ImportStatus::conflict);
    EXPECT_EQ(controller_->getProfiles()[0].name, "Changed");

    auto replaced = controller_->importProfile(file, true);
    EXPECT_EQ(replaced.status, MidiLearnController::ImportStatus::replaced);
    EXPECT_EQ(controller_->getProfiles()[0].name, "Original");
    EXPECT_EQ(controller_->getProfiles().size(), 1u);

    ASSERT_TRUE(controller_->deleteProfile("p1"));
    EXPECT_EQ(controller_->importProfile(file, false).status, MidiLearnController::ImportStatus::imported);
    EXPECT_EQ(controller_->importProfile(root_.getChildFile("missing.json"), false).status,
              MidiLearnController::ImportStatus::invalid);
}

TEST_F(MidiLearnControllerTest, UpdateControlRewritesEveryAssignmentCopyAndTheEngineDecodesTheNewEncoding) {
    controller_->arm(node_->nodeID, "cutoff");
    send(juce::MidiMessage::controllerEvent(1, 20, 64));
    settle();
    ASSERT_EQ(doc_.assignments.size(), 1u);
    const auto profileId = controller_->getProfiles()[0].id;
    auto control = controller_->getProfiles()[0].controls[0];

    control.name = "Cutoff dial";
    control.encoding = Encoding::relSignMag;
    control.kind = ControlKind::encoder;
    EXPECT_TRUE(controller_->updateControl(profileId, control));

    EXPECT_EQ(controller_->getProfiles()[0].controls[0].name, "Cutoff dial");
    EXPECT_EQ(controller_->getProfiles()[0].controls[0].kind, ControlKind::encoder);
    EXPECT_EQ(doc_.assignments[0].specEncoding, Encoding::relSignMag);
    EXPECT_EQ(doc_.assignments[0].specControlName, "Cutoff dial");
    // The message key and layout are the stored ones, never the edit's.
    EXPECT_EQ(controller_->getProfiles()[0].controls[0].message.number, 20);

    send(juce::MidiMessage::controllerEvent(1, 20, 65)); // sign-magnitude -1
    std::vector<RemoteEvent> events;
    remoteEngine_.drainActivity([&](const juce::String&, const RemoteEvent& e) { events.push_back(e); });
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().kind, RemoteEventKind::relativeDelta);
    EXPECT_LT(events.back().value, 0.0f);

    EXPECT_FALSE(controller_->updateControl("nope", control));
    auto unknown = control;
    unknown.id = "nope";
    EXPECT_FALSE(controller_->updateControl(profileId, unknown));
}
