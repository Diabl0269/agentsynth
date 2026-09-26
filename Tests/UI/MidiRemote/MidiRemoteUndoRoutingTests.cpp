// MidiRemoteUndoRoutingTests.cpp -- FRO273 (docs/control/midi-remote.md#undo): the Edit menu's
// Undo/Redo command rows (AppCommands::undo / redo, what Cmd+Z / Cmd+Shift+Z and the menu both
// invoke) act on the controller edit history while the MIDI Remote panel holds keyboard focus, and
// on the project history otherwise. Driven through a real MainComponent's ApplicationCommandManager.
//
// A real keyboard-focus grab needs a native peer (Component::addToDesktop()), which this suite
// never creates (see FocusArbitrationPlaybackDeleteTests.cpp's SurfaceResolverRealFocus). So the
// "panel focused" case uses the panel's own setHoldsUndoFocusForTest() stand-in, and the real-mouse
// test pins the other half: a real synthesized press on a surface cell, with no peer to take
// focus, must leave Cmd+Z on the project -- routing never guesses focus it cannot see.

#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "MainComponent/MainComponent.h"
#include "MidiRemoteMockProvider.h"
#include "MidiRemotePanelTestFixture.h"
#include "Modules/FilterModule.h"
#include "ShortcutManager/AppCommands.h"

using synth::midi::PickTarget;

namespace {

juce::MouseEvent cellMouseEvent(juce::Component& comp) {
    const juce::Point<float> pos(5.0f, 5.0f);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 0.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos,
                            juce::Time::getCurrentTime(), 1, false);
}

class MidiRemoteUndoRoutingTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-undo-routing-" + juce::Uuid().toString());
        mc_ = std::make_unique<MainComponent>(std::make_unique<MidiRemoteMockProvider>(),
                                              synth::AIProviderRegistry::createDefault(),
                                              synth::ControllerProfileStore(root_));
        mc_->setSize(1400, 900);
        mc_->setVisible(true);
        mc_->newPatchForTest();

        auto& editor = mc_->getGraphEditor();
        node_ = editor.getAudioEngine().getGraph().addNode(std::make_unique<FilterModule>());
        editor.updateComponents();

        synth::ControllerProfile profile;
        profile.id = "p1";
        profile.name = "Launchkey";
        profile.input.identifier = synth::midi::hostSourceKey();
        synth::Control knob;
        knob.id = "k";
        knob.name = "Knob 1";
        knob.message.type = synth::MessageType::cc;
        knob.message.channel = 1;
        knob.message.number = 21;
        profile.controls = {knob};
        ASSERT_TRUE(controller().addProfile(profile));

        // One project edit (a parameter assignment) and one controller edit (a rename) after it, so
        // each history has something only it can undo.
        ASSERT_EQ(controller().assignControl("p1", "k", PickTarget::parameter(node_->nodeID, "cutoff")),
                  synth::midi::AssignStatus::assigned);
        auto renamed = controller().getProfiles().front();
        renamed.name = "Renamed";
        ASSERT_TRUE(controller().updateProfile(renamed, "Rename controller"));

        mc_->getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::MidiRemote);
        panel().selectForTest("p1", "k");
    }

    void TearDown() override {
        mc_.reset();
        root_.deleteRecursively();
    }

    synth::midi::MidiLearnController& controller() { return mc_->getMidiLearnControllerForTest(); }
    synth::ui::MidiRemotePanelComponent& panel() { return mc_->getBottomDock().getMidiRemotePanel(); }
    juce::String profileName() { return controller().getProfiles().front().name; }
    int projectAssignments() { return controller().countProjectAssignmentsForProfile("p1"); }
    bool invoke(juce::CommandID id) { return mc_->getCommandManager().invokeDirectly(id, false); }

    BottomDockActiveTabResetGuardMDT resetGuard_;
    juce::File root_;
    std::unique_ptr<MainComponent> mc_;
    juce::AudioProcessorGraph::Node::Ptr node_;
};

} // namespace

TEST_F(MidiRemoteUndoRoutingTest, PanelFocusedUndoAndRedoActOnTheControllerHistoryOnly) {
    panel().setHoldsUndoFocusForTest(true);
    const bool projectCouldUndo = mc_->getUndoManager().canUndo();
    ASSERT_TRUE(projectCouldUndo);

    ASSERT_TRUE(invoke(AppCommands::undo));
    EXPECT_EQ(profileName(), "Launchkey") << "the rename was undone";
    EXPECT_EQ(projectAssignments(), 1) << "the project history was not touched";
    EXPECT_TRUE(mc_->getUndoManager().canUndo());

    ASSERT_TRUE(invoke(AppCommands::redo));
    EXPECT_EQ(profileName(), "Renamed");
    EXPECT_EQ(projectAssignments(), 1);
}

TEST_F(MidiRemoteUndoRoutingTest, PanelNotFocusedUndoActsOnTheProjectHistory) {
    panel().setHoldsUndoFocusForTest(false); // focus is on the canvas (or anywhere else)

    ASSERT_TRUE(invoke(AppCommands::undo));
    EXPECT_EQ(projectAssignments(), 0) << "the project's last edit (the assignment) was undone";
    EXPECT_EQ(profileName(), "Renamed") << "the controller history was not touched";
    EXPECT_TRUE(controller().canUndoProfileEdit());
}

// The real mouse path: a synthesized press on a surface cell selects it (its production mouseDown).
// With no native peer nothing can actually take keyboard focus, so the real-focus resolver must
// still say "not focused" and Cmd+Z stays on the project -- never a guess in the panel's favour.
TEST_F(MidiRemoteUndoRoutingTest, ARealCellPressWithoutANativePeerLeavesUndoOnTheProject) {
    panel().setHoldsUndoFocusForTest(std::nullopt);
    auto* cell = const_cast<synth::ui::ControllerSurfaceCell*>(panel().findSurfaceCellForTest("k"));
    ASSERT_NE(cell, nullptr);
    cell->mouseDown(cellMouseEvent(*cell));
    cell->mouseUp(cellMouseEvent(*cell));

    ASSERT_FALSE(panel().holdsUndoFocus()) << "no peer, so no real focus";
    ASSERT_TRUE(invoke(AppCommands::undo));
    EXPECT_EQ(projectAssignments(), 0);
    EXPECT_EQ(profileName(), "Renamed");
}

TEST_F(MidiRemoteUndoRoutingTest, FocusedPanelWithAnEmptyControllerHistoryDoesNotFallThroughToTheProject) {
    panel().setHoldsUndoFocusForTest(true);
    while (controller().canUndoProfileEdit())
        ASSERT_TRUE(invoke(AppCommands::undo));
    ASSERT_EQ(projectAssignments(), 1);

    ASSERT_TRUE(invoke(AppCommands::undo));
    EXPECT_EQ(projectAssignments(), 1) << "the user is looking at the panel -- a canvas edit is never undone";
}

TEST_F(MidiRemoteUndoRoutingTest, ToolbarCueShowsWhileThePanelIsFocused) {
    panel().setHoldsUndoFocusForTest(true);
    EXPECT_TRUE(panel().getToolbarForTest().getUndoHint().endsWith("undoes: Rename controller"));
    panel().setHoldsUndoFocusForTest(false);
    EXPECT_TRUE(panel().getToolbarForTest().getUndoHint().isEmpty());
}
