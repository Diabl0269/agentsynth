// Concern: FRO278's select next/previous module and track commands, dispatched through the real
// command manager. Deliberately NOT routed by resolveEditSurface(): each pair works the same
// wherever focus is, which these tests pin by forcing the "wrong" surface first.
#include "FocusArbitrationTestFixture.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

NodeID addModuleAt(MainComponent& mc, std::unique_ptr<juce::AudioProcessor> processor, int x) {
    auto node = mc.getAudioEngine().getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", 0);
    mc.getGraphEditor().updateComponents();
    return node->nodeID;
}

bool invoke(MainComponent& mc, juce::CommandID id) { return mc.getCommandManager().invokeDirectly(id, false); }

} // namespace

TEST_F(FocusArbitrationTest, SelectNextAndPreviousModuleStepTheGraphSelectionFromAnySurface) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1600, 900);
    const auto left = addModuleAt(mc, std::make_unique<OscillatorModule>(), 0);
    const auto right = addModuleAt(mc, std::make_unique<FilterModule>(), 400);
    // Start from a known module rather than nothing: the app may place modules of its own.
    mc.getGraphEditor().selectModule(left, false);

    // Focus pretends to be on the timeline: the module pair must not care.
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    ASSERT_TRUE(invoke(mc, AppCommands::selectNextModule));
    const auto afterNext = mc.getGraphEditor().getSelectedNodes();
    ASSERT_EQ(afterNext.size(), 1u);
    EXPECT_NE(afterNext.front(), left) << "next moved off the starting module";
    ASSERT_TRUE(invoke(mc, AppCommands::selectPreviousModule));
    EXPECT_EQ(mc.getGraphEditor().getSelectedNodes(), std::vector<NodeID>{left});
    (void)right;
}

TEST_F(FocusArbitrationTest, SelectModuleCommandsReportFalseOnAnEmptyCanvas) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1600, 900);
    // Detach every card first, the way every graph-replacing seam does: clearing the graph under
    // live cards frees the keyboard module whose MidiKeyboardState a card's keyboard still listens
    // to, and the card's destructor then reads freed memory (a heap-use-after-free under ASAN).
    mc.getGraphEditor().detachAllModuleComponents();
    mc.getAudioEngine().getGraph().clear();
    mc.getGraphEditor().updateComponents();
    ASSERT_EQ(mc.getGraphEditor().getModuleComponents().size(), 0);

    EXPECT_FALSE(invoke(mc, AppCommands::selectNextModule));
    EXPECT_FALSE(invoke(mc, AppCommands::selectPreviousModule));
}

TEST_F(FocusArbitrationTest, SelectTrackCommandsAreInactiveWhileTheTimelineIsHidden) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1600, 900);
    mc.getTimelineDoc().addTrack(synth::TrackKind::Midi, "A");
    ASSERT_FALSE(mc.isTimelineConfiguredVisible());

    EXPECT_FALSE(commandIsActive(mc, AppCommands::selectNextTrack));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::selectPreviousTrack));
    EXPECT_TRUE(commandIsActive(mc, AppCommands::selectNextModule)) << "the module pair needs no panel";
}

TEST_F(FocusArbitrationTest, SelectNextAndPreviousTrackMoveTheFocusedTrackRow) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1600, 900);
    auto& doc = mc.getTimelineDoc();
    doc.addTrack(synth::TrackKind::Midi, "A");
    doc.addTrack(synth::TrackKind::Midi, "B");
    doc.addTrack(synth::TrackKind::Midi, "C");
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());
    auto& panel = mc.getTimelinePanel();
    ASSERT_EQ(panel.getTrackHeaderCount(), 3);
    ASSERT_EQ(panel.getFocusedTrackIndexForTest(), -1);

    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph); // must not matter
    ASSERT_TRUE(invoke(mc, AppCommands::selectNextTrack));
    EXPECT_EQ(panel.getFocusedTrackIndexForTest(), 0) << "nothing focused starts at the first row";
    ASSERT_TRUE(invoke(mc, AppCommands::selectNextTrack));
    ASSERT_TRUE(invoke(mc, AppCommands::selectNextTrack));
    ASSERT_TRUE(invoke(mc, AppCommands::selectNextTrack));
    EXPECT_EQ(panel.getFocusedTrackIndexForTest(), 2) << "clamps at the last row";
    ASSERT_TRUE(invoke(mc, AppCommands::selectPreviousTrack));
    EXPECT_EQ(panel.getFocusedTrackIndexForTest(), 1);
}

TEST_F(FocusArbitrationTest, SelectTrackCommandsReportFalseWithNoTracks) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());
    ASSERT_EQ(mc.getTimelinePanel().getTrackHeaderCount(), 0);

    EXPECT_FALSE(invoke(mc, AppCommands::selectNextTrack));
    EXPECT_FALSE(invoke(mc, AppCommands::selectPreviousTrack));
}
