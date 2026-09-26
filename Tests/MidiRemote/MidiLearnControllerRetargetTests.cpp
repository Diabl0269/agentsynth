// FRO240 (docs/control/midi-remote.md#replace-and-duplicate): "Replace with..." keeps a module's
// MIDI Remote mappings, retargeted onto the new node; duplicate/copy-paste still does NOT copy
// them (pinned here, not just described). Drives the real GraphEditor::replaceModule()/
// duplicateSelection() entry points -- the same ones the canvas context menu calls -- rather than
// only MidiLearnController::retargetNode() directly, so a regression in the wiring (GraphEditor's
// onModuleReplaced callback, or AppUndoManager's combined recorder) fails a test here too. Same
// HostMode::Hosted / temp-profile-directory / send-settle-drain idiom as MidiLearnControllerTests.cpp.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiLearnController.h"
#include "Modules/FilterModule.h"
#include "Modules/VCAModule.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

using namespace synth;
using namespace synth::midi;
using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

// AudioEngine::updateModuleNames() numbers instances ("Filter 1", "Filter 2", ...), so this
// matches by dynamic_cast rather than getName() equality.
NodeID nodeIdOfType(juce::AudioProcessorGraph& graph, NodeID exclude = {}) {
    for (auto* node : graph.getNodes())
        if (node->nodeID != exclude && dynamic_cast<FilterModule*>(node->getProcessor()) != nullptr)
            return node->nodeID;
    return {};
}

ModuleComponent* findModuleComp(GraphEditor& editor, juce::AudioProcessor* proc) {
    for (auto* mc : editor.getModuleComponents())
        if (mc->getModule() == proc)
            return mc;
    return nullptr;
}

class MidiLearnControllerRetargetTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-midilearncontroller-retarget-tests-" + juce::Uuid().toString());
        root_.deleteRecursively();

        engine_ = std::make_unique<AudioEngine>(AudioEngine::HostMode::Hosted);
        graphEditor_ = std::make_unique<GraphEditor>(*engine_, &undo_);
        // As MainComponentSetup.cpp does: without it an undo/redo restore frees processors without
        // detaching their cards first, and a card left on a freed (then reused) address crashes
        // ~GraphEditor.
        undo_.setGraphEditor(graphEditor_.get());
        controller_ = std::make_unique<MidiLearnController>(*engine_, *graphEditor_, remoteEngine_, doc_, undo_,
                                                            statusBar_, synth::ControllerProfileStore(root_));
        remoteEngine_.setClock([this] { return fakeNowMs_; });

        // Same wiring MainComponentSetup.cpp does for "Replace with..." (FRO240).
        graphEditor_->setMidiRemoteProjectDocForUndo(&doc_);
        graphEditor_->onModuleReplaced = [this](const juce::String& oldUuid, NodeID newNodeId) {
            controller_->retargetNode(oldUuid, newNodeId);
        };
        graphEditor_->onMidiRemoteDocRestored = [this] { controller_->publishAssignments(); };

        graphEditor_->setSize(800, 600);
        filterNode_ = engine_->getGraph().addNode(std::make_unique<FilterModule>());
        graphEditor_->updateComponents();
    }

    void TearDown() override {
        remoteEngine_.endAllGestures();
        // Cards before the processors they bind: filterNode_ (declared last, so destroyed first) can
        // be the only owner of a replaced module's processor.
        controller_.reset();
        graphEditor_.reset();
        filterNode_ = nullptr;
        root_.deleteRecursively();
    }

    void send(const juce::MidiMessage& message) { remoteEngine_.handleMessage(hostSourceKey(), message); }

    void settle() {
        remoteEngine_.drain();
        fakeNowMs_ += kLearnSettleMs + 1.0;
        remoteEngine_.drain();
    }

    // Learns CC 20 -> the current filter node's "cutoff", settling the learn.
    void learnCutoff() {
        controller_->arm(filterNode_->nodeID, "cutoff");
        send(juce::MidiMessage::controllerEvent(1, 20, 64));
        settle();
        ASSERT_EQ(doc_.assignments.size(), 1u);
    }

    juce::RangedAudioParameter* rangedParam(juce::AudioProcessor* proc, const juce::String& paramId) {
        for (auto* p : proc->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
                ranged != nullptr && ranged->paramID == paramId)
                return ranged;
        return nullptr;
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
    juce::AudioProcessorGraph::Node::Ptr filterNode_;
};

} // namespace

TEST_F(MidiLearnControllerRetargetTest, ReplaceWithSameKindModuleRetargetsTheAssignmentAndACcMovesTheNewCutoff) {
    remoteEngine_.setDefaultTakeover(synth::Takeover::jump);
    learnCutoff();
    const juce::String oldUuid = doc_.assignments[0].target.parameter.nodeUuid;
    ASSERT_FALSE(oldUuid.isEmpty());

    auto* filterComp = findModuleComp(*graphEditor_, filterNode_->getProcessor());
    ASSERT_NE(filterComp, nullptr);
    graphEditor_->replaceModule(filterComp, "Filter"); // same module type -- "cutoff" still exists

    auto& graph = engine_->getGraph();
    const NodeID newNodeId = nodeIdOfType(graph);
    ASSERT_NE(newNodeId.uid, 0u);
    auto* newNode = graph.getNodeForId(newNodeId);
    const juce::String newUuid = newNode->properties["uuid"].toString();
    ASSERT_FALSE(newUuid.isEmpty());
    EXPECT_NE(newUuid, oldUuid);

    ASSERT_EQ(doc_.assignments.size(), 1u) << "replace must not drop or duplicate the assignment";
    EXPECT_EQ(doc_.assignments[0].target.parameter.nodeUuid, newUuid)
        << "the assignment must follow the replacement module, not the one that's gone";
    EXPECT_EQ(doc_.assignments[0].target.parameter.paramId, "cutoff");

    // The engine's own cache must already be republished -- a CC drives the NEW module's cutoff
    // with no unrelated graph change in between (same assertion shape as
    // MidiLearnControllerTests.cpp's LearnThenImmediateCcDrivesTheParameterWithoutAnExplicitReconcile).
    auto* cutoff = rangedParam(newNode->getProcessor(), "cutoff");
    ASSERT_NE(cutoff, nullptr);
    const float before = cutoff->getValue();
    send(juce::MidiMessage::controllerEvent(1, 20, 127));
    remoteEngine_.drain();
    EXPECT_NE(cutoff->getValue(), before) << "the retargeted assignment must already resolve against the new node";

    fakeNowMs_ += kGestureIdleMs + 1.0;
    remoteEngine_.drain();
}

TEST_F(MidiLearnControllerRetargetTest, ReplaceWithAModuleLackingTheParamIdLeavesTheAssignmentOrphaned) {
    learnCutoff();
    const juce::String oldUuid = doc_.assignments[0].target.parameter.nodeUuid;

    auto* filterComp = findModuleComp(*graphEditor_, filterNode_->getProcessor());
    ASSERT_NE(filterComp, nullptr);
    graphEditor_->replaceModule(filterComp, "VCA"); // VCA has no "cutoff" parameter

    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].target.parameter.nodeUuid, oldUuid)
        << "nothing on the new module answers to \"cutoff\" -- the assignment stays exactly where it was (orphaned)";
}

TEST_F(MidiLearnControllerRetargetTest, UndoAfterReplaceRestoresTheOldNodeAndTheOldAssignmentTargetAsOneStep) {
    learnCutoff();
    const juce::String oldUuid = doc_.assignments[0].target.parameter.nodeUuid;

    auto* filterComp = findModuleComp(*graphEditor_, filterNode_->getProcessor());
    ASSERT_NE(filterComp, nullptr);
    graphEditor_->replaceModule(filterComp, "Filter");

    auto& graph = engine_->getGraph();
    const NodeID newNodeId = nodeIdOfType(graph);
    ASSERT_NE(newNodeId.uid, 0u);
    const juce::String newUuid = graph.getNodeForId(newNodeId)->properties["uuid"].toString();
    ASSERT_NE(doc_.assignments[0].target.parameter.nodeUuid, oldUuid);

    ASSERT_TRUE(undo_.canUndo());
    undo_.undo();

    // One Cmd+Z: the old node is back AND the assignment points at it again.
    const NodeID restoredNodeId = nodeIdOfType(graph);
    ASSERT_NE(restoredNodeId.uid, 0u);
    EXPECT_EQ(graph.getNodeForId(restoredNodeId)->properties["uuid"].toString(), oldUuid);
    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].target.parameter.nodeUuid, oldUuid);
    EXPECT_NE(doc_.assignments[0].target.parameter.nodeUuid, newUuid);
}

TEST_F(MidiLearnControllerRetargetTest, RedoReappliesTheReplaceAndTheRetargetTogether) {
    learnCutoff();
    const juce::String oldUuid = doc_.assignments[0].target.parameter.nodeUuid;

    auto* filterComp = findModuleComp(*graphEditor_, filterNode_->getProcessor());
    ASSERT_NE(filterComp, nullptr);
    graphEditor_->replaceModule(filterComp, "Filter");

    auto& graph = engine_->getGraph();
    const juce::String newUuidBeforeUndo = graph.getNodeForId(nodeIdOfType(graph))->properties["uuid"].toString();

    undo_.undo();
    ASSERT_TRUE(undo_.canRedo());
    undo_.redo();

    const NodeID redoneNodeId = nodeIdOfType(graph);
    ASSERT_NE(redoneNodeId.uid, 0u);
    const juce::String redoneUuid = graph.getNodeForId(redoneNodeId)->properties["uuid"].toString();
    EXPECT_NE(redoneUuid, oldUuid);
    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].target.parameter.nodeUuid, redoneUuid);
    // FRO240's uuid is assigned once, at retarget time, and never reissued by a later redo of the
    // SAME action -- the graph SnapshotAction restores the exact node->properties it snapshotted.
    EXPECT_EQ(redoneUuid, newUuidBeforeUndo);
}

TEST_F(MidiLearnControllerRetargetTest, DuplicatingAMappedModuleDoesNotCopyItsAssignment) {
    learnCutoff();
    const juce::String originalUuid = doc_.assignments[0].target.parameter.nodeUuid;

    graphEditor_->setSelectedNodes({filterNode_->nodeID});
    ASSERT_TRUE(graphEditor_->duplicateSelection());

    auto& graph = engine_->getGraph();
    const NodeID copyId = nodeIdOfType(graph, filterNode_->nodeID);
    ASSERT_NE(copyId.uid, 0u) << "duplicateSelection() must have created a second Filter node";
    const juce::String copyUuid = graph.getNodeForId(copyId)->properties["uuid"].toString();

    // docs/control/midi-remote.md#replace-and-duplicate: still exactly one assignment, still
    // pointed at the ORIGINAL node -- the copy has no mapping of its own.
    ASSERT_EQ(doc_.assignments.size(), 1u);
    EXPECT_EQ(doc_.assignments[0].target.parameter.nodeUuid, originalUuid);
    if (!copyUuid.isEmpty())
        EXPECT_NE(doc_.assignments[0].target.parameter.nodeUuid, copyUuid);
}
