// FRO227: the Mixer edit surface -- MainComponent::resolveEditSurface() previously knew
// Graph/TimelineClips/PianoRoll only, so Cmd+C/V/D/X/R (and Select All/zoom, routed the same way)
// with the mixer focused fell through to Graph and acted on the canvas selection instead. See
// docs/control/shortcuts.md's "Surface routing" section and docs/timeline/focus.md for the
// production rule this pins.
//
// What this file deliberately does NOT do, and why: a real, OS-tracked keyboard-focus grab onto a
// mixer column needs a native peer (juce::Component::addToDesktop()), which this whole test suite
// avoids for the documented headless-CI flakiness/hang risk -- see
// FocusArbitrationPlaybackDeleteTests.cpp's SurfaceResolverRealFocus and
// Tests/UI/Timeline/TimelineTrackFocusTests.cpp's identical caveat. So every behavioural test below
// drives the Mixer surface through MainComponent::setEditSurfaceOverrideForTest(), the same
// headless stand-in every other surface in this suite relies on; MixerResolverRoundTripAndGating
// extends SurfaceResolverRealFocus's own resolver test to cover Mixer, including a best-effort
// mixerDock.getMixerPanel().grabKeyboardFocus() call that documents the panel-visibility gate wins
// regardless of whether that grab actually lands anywhere in this environment.
#include "FocusArbitrationTestFixture.h"

// ============================================================================
// 1. resolveEditSurface() -- Mixer joins the override round trip, and the dock's own open/active
//    gating (mirroring the timeline panel's visibility gate) wins over a stale focus pointer.
// ============================================================================

TEST_F(FocusArbitrationTest, MixerResolverRoundTripAndGating) {
    MainComponent mc(std::make_unique<FocusMockProvider>());

    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Mixer);
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Mixer);
    mc.setEditSurfaceOverrideForTest(std::nullopt);
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph)
        << "clearing the override falls back to the real-focus resolver; the dock starts hidden";

    // Best-effort real-focus attempt: the mixer panel is never added to the desktop, so
    // grabKeyboardFocus() may silently no-op here (SurfaceResolverRealFocus's own caveat). Either
    // way the dock's own open-and-on-the-Mixer-tab gate must win, exactly like isTimelineVisible
    // gates the two timeline surfaces above.
    mc.getMixerDock().getMixerPanel().grabKeyboardFocus();
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph)
        << "the dock is closed -- a stale/no-op focus pointer must never resolve to Mixer";
}

// ============================================================================
// 2. Copy/Paste/Duplicate/Cut/Repeat are all inactive on Mixer, and a direct invoke is refused
//    without ever touching the graph -- same shape as
//    FocusArbitrationClipboardTests.cpp's CutInactiveAndRepeatUnsupportedOnGraph.
// ============================================================================

TEST_F(FocusArbitrationTest, MixerSurfaceClipboardAndRepeatInactive) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Mixer);

    auto& editor = mc.getGraphEditor();
    auto& graph = mc.getAudioEngine().getGraph();

    // A real graph selection, so a Mixer-surface command that wrongly fell through to Graph would
    // have something to act on -- the assertions below would then fail loudly instead of trivially.
    auto node = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
    ASSERT_NE(node, nullptr);
    editor.setSelectedNodes({node->nodeID});
    ASSERT_EQ(editor.getSelectionCount(), 1);
    const int nodesBefore = graph.getNumNodes();

    for (auto cmdId : {AppCommands::copySelection, AppCommands::pasteSelection, AppCommands::duplicateSelection,
                       AppCommands::cutSelection, AppCommands::repeatSelection})
        EXPECT_FALSE(commandIsActive(mc, cmdId)) << "command " << cmdId << " must be inactive on the Mixer surface";

    auto& cm = mc.getCommandManager();
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::copySelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::pasteSelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::duplicateSelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::repeatSelection, false));
    EXPECT_FALSE(mc.performRepeatSelection(3)) << "the direct/scripted door agrees with the greyed-out menu row";

    // Nothing moved: the graph selection, the graph itself, the clipboard and the undo stack are
    // all exactly as they were before every one of those (refused) invocations.
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);
    EXPECT_EQ(editor.getSelectionCount(), 1);
    EXPECT_FALSE(editor.canPaste()) << "Copy never ran -- the graph clipboard must still be empty";
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "nothing was invoked -- there is nothing to undo";
}

// ============================================================================
// 3. Zoom is inactive on both axes -- unlike Graph, which is only inactive on the vertical pair
//    (see FocusArbitrationZoomGridTests.cpp).
// ============================================================================

TEST_F(FocusArbitrationTest, MixerSurfaceZoomInactiveBothAxes) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Mixer);

    for (auto cmdId : {AppCommands::zoomInHorizontal, AppCommands::zoomOutHorizontal, AppCommands::zoomInVertical,
                       AppCommands::zoomOutVertical})
        EXPECT_FALSE(commandIsActive(mc, cmdId)) << "zoom command " << cmdId << " must be inactive on Mixer";

    auto& cm = mc.getCommandManager();
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::zoomInHorizontal, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::zoomOutVertical, false));
}

// ============================================================================
// 4. Select All is a no-op on Mixer -- it stays reachable (unlike the clipboard verbs, Select All
//    is deliberately always-active on every surface -- see MainComponentCommandTable.cpp's row
//    comment) but must leave the graph selection untouched rather than falling through to it.
// ============================================================================

TEST_F(FocusArbitrationTest, MixerSurfaceSelectAllLeavesGraphSelectionUnchanged) {
    MainComponent mc(std::make_unique<FocusMockProvider>());

    auto& editor = mc.getGraphEditor();
    auto& graph = mc.getAudioEngine().getGraph();
    auto nodeA = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
    auto nodeB = graph.addNode(synth::AIStateMapper::createModule("Filter"));
    ASSERT_NE(nodeA, nullptr);
    ASSERT_NE(nodeB, nullptr);
    editor.setSelectedNodes({nodeA->nodeID});
    ASSERT_EQ(editor.getSelectionCount(), 1);

    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Mixer);
    EXPECT_TRUE(commandIsActive(mc, AppCommands::selectAllModules)) << "Select All stays always-active, like every "
                                                                       "other surface";

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::selectAllModules, false));
    EXPECT_EQ(editor.getSelectionCount(), 1) << "Mixer has no select-all model -- the graph selection must be "
                                                "untouched, not widened to every module";
}
