// FRO141 (docs/control/midi-remote.md#focus-bank): MidiLearnController's canvas-selection polling
// (focusBankWatcher_, MidiLearnControllerFocusBank.cpp) -- selecting a module binds a profile's
// focus-bank controls to its parameters in on-card order, selecting a different module rebinds,
// and deselecting (or selecting several modules) clears the bindings. Drives the real
// GraphEditor::selectModule/clearSelection API and lets the real 200 ms UiWatcher timer tick via
// juce::MessageManager::runDispatchLoopUntil, exactly like MidiLearnControllerHostedParameterTests.cpp,
// rather than calling the polling function directly. Mirrors MidiLearnControllerTests.cpp's fixture.
// Suite name contains "MidiRemote" per the ship-task --gtest_filter convention.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiLearnController.h"
#include "Modules/FilterModule.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <memory>

using namespace synth;
using namespace synth::midi;

namespace {

// Longer than focusBankWatcher_'s 200 ms tick, so the dispatch loop is guaranteed to run it at
// least once (matches this codebase's established way of driving a real Timer in a headless test --
// see MidiLearnControllerHostedParameterTests.cpp).
constexpr int kTickWaitMs = 260;

class MidiLearnControllerFocusBankTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-focusbank-tests-" + juce::Uuid().toString());
        root_.deleteRecursively();

        engine_ = std::make_unique<AudioEngine>(AudioEngine::HostMode::Hosted);
        graphEditor_ = std::make_unique<GraphEditor>(*engine_);
        controller_ = std::make_unique<MidiLearnController>(*engine_, *graphEditor_, remoteEngine_, doc_, undo_,
                                                            statusBar_, synth::ControllerProfileStore(root_));

        nodeA_ = engine_->getGraph().addNode(std::make_unique<FilterModule>());
        nodeB_ = engine_->getGraph().addNode(std::make_unique<FilterModule>());
        graphEditor_->updateComponents();

        ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Bank Controller";
        profile.input.identifier = hostSourceKey();
        // Two focus-bank controls, deliberately declared out of layout order, to prove pairing goes
        // by (row, col), not declaration order.
        Control second;
        second.id = "bank-2";
        second.name = "Bank 2";
        second.message.type = MessageType::cc;
        second.message.channel = 1;
        second.message.number = 21;
        second.focusBank = true;
        second.layout.row = 0;
        second.layout.col = 1;
        Control first;
        first.id = "bank-1";
        first.name = "Bank 1";
        first.message.type = MessageType::cc;
        first.message.channel = 1;
        first.message.number = 20;
        first.focusBank = true;
        first.layout.row = 0;
        first.layout.col = 0;
        profile.controls = {second, first};
        ASSERT_TRUE(controller_->addProfile(profile));
    }

    void TearDown() override {
        remoteEngine_.endAllGestures();
        root_.deleteRecursively();
    }

    static void tick() { juce::MessageManager::getInstance()->runDispatchLoopUntil(kTickWaitMs); }

    juce::String nodeUuid(juce::AudioProcessorGraph::Node::Ptr node) { return node->properties["uuid"].toString(); }

    juce::File root_;
    RemoteEngine remoteEngine_;
    synth::MidiRemoteProjectDoc doc_;
    AppUndoManager undo_;
    StatusBarComponent statusBar_;
    std::unique_ptr<AudioEngine> engine_;
    std::unique_ptr<GraphEditor> graphEditor_;
    std::unique_ptr<MidiLearnController> controller_;
    juce::AudioProcessorGraph::Node::Ptr nodeA_;
    juce::AudioProcessorGraph::Node::Ptr nodeB_;
};

} // namespace

TEST_F(MidiLearnControllerFocusBankTest, NothingSelectedMeansNoTransientBindings) {
    tick();
    EXPECT_TRUE(remoteEngine_.getTransientAssignments().empty());
}

TEST_F(MidiLearnControllerFocusBankTest, SelectingAModuleBindsTheBankInCardOrder) {
    graphEditor_->selectModule(nodeA_->nodeID, false);
    tick();

    const auto transients = remoteEngine_.getTransientAssignments();
    ASSERT_EQ(transients.size(), 2u) << "both focus-bank controls pair with FilterModule's first two parameters";
    for (const auto& a : transients)
        EXPECT_EQ(a.target.parameter.nodeUuid, nodeUuid(nodeA_));

    // Bank order is (row, col): "bank-1" (col 0) drives on-card parameter 0 ("cutoff", the first
    // FilterModule::addParameter call), "bank-2" (col 1) drives parameter 1 ("resonance").
    const auto findByControl = [&](const juce::String& controlId) -> const Assignment* {
        for (const auto& a : transients)
            if (a.control.controlId == controlId)
                return &a;
        return nullptr;
    };
    const auto* first = findByControl("bank-1");
    const auto* second = findByControl("bank-2");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->target.parameter.paramId, "cutoff");
    EXPECT_EQ(second->target.parameter.paramId, "resonance");
    EXPECT_EQ(first->id, "focus:p1:bank-1");
}

TEST_F(MidiLearnControllerFocusBankTest, SelectingADifferentModuleRebindsToIt) {
    graphEditor_->selectModule(nodeA_->nodeID, false);
    tick();
    ASSERT_FALSE(remoteEngine_.getTransientAssignments().empty());

    graphEditor_->selectModule(nodeB_->nodeID, false);
    tick();

    const auto transients = remoteEngine_.getTransientAssignments();
    ASSERT_FALSE(transients.empty());
    for (const auto& a : transients)
        EXPECT_EQ(a.target.parameter.nodeUuid, nodeUuid(nodeB_)) << "the bank must have moved off node A entirely";
}

TEST_F(MidiLearnControllerFocusBankTest, ClearSelectionClearsTheBindings) {
    graphEditor_->selectModule(nodeA_->nodeID, false);
    tick();
    ASSERT_FALSE(remoteEngine_.getTransientAssignments().empty());

    graphEditor_->clearSelection();
    tick();

    EXPECT_TRUE(remoteEngine_.getTransientAssignments().empty());
}

TEST_F(MidiLearnControllerFocusBankTest, SelectingTwoModulesClearsTheBindings) {
    graphEditor_->selectModule(nodeA_->nodeID, false);
    tick();
    ASSERT_FALSE(remoteEngine_.getTransientAssignments().empty());

    graphEditor_->selectModule(nodeB_->nodeID, true); // additive -- now two selected
    tick();

    EXPECT_TRUE(remoteEngine_.getTransientAssignments().empty());
}

// FRO141 (docs/control/midi-remote-ui.md): the Inspector's "Follow selection (focus bank)" toggle
// goes through MidiLearnController::updateControl -- the same profile-edit path as name/kind/
// encoding -- so it is one step on the controller edit history, undoable there.
TEST_F(MidiLearnControllerFocusBankTest, TogglingFocusBankThroughUpdateControlIsUndoable) {
    // The fixture's own controls are already focus-bank controls -- flip one OFF to exercise the
    // toggle in the other direction (the inspector's toggle works either way).
    auto control = controller_->getProfiles()[0].controls[0];
    ASSERT_TRUE(control.focusBank);
    control.focusBank = false;

    EXPECT_TRUE(controller_->updateControl("p1", control));
    EXPECT_FALSE(controller_->getProfiles()[0].controls[0].focusBank);

    ASSERT_TRUE(controller_->canUndoProfileEdit());
    EXPECT_TRUE(controller_->undoProfileEdit());
    EXPECT_TRUE(controller_->getProfiles()[0].controls[0].focusBank);

    EXPECT_TRUE(controller_->redoProfileEdit());
    EXPECT_FALSE(controller_->getProfiles()[0].controls[0].focusBank);
}

// FRO141: never persisted -- a focus-bank binding must never leak into MidiRemoteProjectDoc, so
// saving a project (doc_.toVar()) can never contain it.
TEST_F(MidiLearnControllerFocusBankTest, FocusBankBindingsAreNeverWrittenToTheProjectDoc) {
    graphEditor_->selectModule(nodeA_->nodeID, false);
    tick();
    ASSERT_FALSE(remoteEngine_.getTransientAssignments().empty());

    EXPECT_TRUE(doc_.assignments.empty());
    const juce::var saved = doc_.toVar();
    const auto* assignmentsArr = saved.getDynamicObject()->getProperty("assignments").getArray();
    ASSERT_NE(assignmentsArr, nullptr);
    EXPECT_TRUE(assignmentsArr->isEmpty()) << "a focus-bank binding must never appear in a saved project";
}

// Ticking "Follow selection" on a control while a module is already selected binds it on the next
// poll -- the user shouldn't have to click another module first.
TEST_F(MidiLearnControllerFocusBankTest, TogglingFocusBankWhileAModuleIsSelectedRebindsWithoutReselecting) {
    graphEditor_->selectModule(nodeA_->nodeID, false);
    tick();
    ASSERT_EQ(remoteEngine_.getTransientAssignments().size(), 2u);

    auto control = controller_->getProfiles()[0].controls[0];
    control.focusBank = false;
    ASSERT_TRUE(controller_->updateControl("p1", control));
    tick();
    EXPECT_EQ(remoteEngine_.getTransientAssignments().size(), 1u);

    ASSERT_TRUE(controller_->undoProfileEdit());
    tick();
    EXPECT_EQ(remoteEngine_.getTransientAssignments().size(), 2u);
}

// The panel redraws on onChanged: a rebind must announce itself, or a bank cell keeps its stale label.
TEST_F(MidiLearnControllerFocusBankTest, ARebindAnnouncesOnChangedSoThePanelRedraws) {
    int changes = 0;
    controller_->onChanged = [&changes] { ++changes; };
    graphEditor_->selectModule(nodeA_->nodeID, false);
    tick();
    EXPECT_GE(changes, 1);

    const int afterBind = changes;
    graphEditor_->clearSelection();
    tick();
    EXPECT_GT(changes, afterBind);
}
