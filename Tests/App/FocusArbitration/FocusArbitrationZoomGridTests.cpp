// Concern: grid (snap) commands and zoom commands routed by resolveEditSurface(), the
// inactive-while-hidden gate, and the Preferences natural-scrolling/scroll-to-zoom toggles
// reaching both the timeline and the roll live.
#include "FocusArbitrationTestFixture.h"

// ============================================================================
// 10. Grid (snap) commands — one shared value, gated on the panel being on screen
// ============================================================================

TEST_F(FocusArbitrationTest, SnapCommandsDriveThePanelsSharedGrid) {
    // setSnapValue()/cycleSnapValue() persist, so the two keys they write are restored afterwards.
    PersistedKeysGuard guard({"timelineSnap", "timelineSnapEnabled"});

    using Snap = synth::ui::TimelineViewState::Snap;
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1200, 800);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isBottomDockConfiguredVisible());

    auto& cm = mc.getCommandManager();
    auto& view = mc.getTimelinePanel().getViewState();

    // Absolute setters. Each also re-arms the master snap switch — asking for a division means
    // "snap to THIS", the same thing the snap combo does.
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapSetEighth, false));
    EXPECT_EQ(view.snap, Snap::Eighth);
    EXPECT_TRUE(view.snapEnabled);

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapSetWhole, false));
    EXPECT_EQ(view.snap, Snap::Whole);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapSetHalf, false));
    EXPECT_EQ(view.snap, Snap::Half);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapSetQuarter, false));
    EXPECT_EQ(view.snap, Snap::Quarter);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapSetSixteenth, false));
    EXPECT_EQ(view.snap, Snap::Sixteenth);
    // The finer half of the row (Ctrl+Shift+6/7/8), folded into the same perform arm.
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapSetThirtySecond, false));
    EXPECT_EQ(view.snap, Snap::ThirtySecond);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapSetSixtyFourth, false));
    EXPECT_EQ(view.snap, Snap::SixtyFourth);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapSetHundredTwentyEighth, false));
    EXPECT_EQ(view.snap, Snap::HundredTwentyEighth);

    // Cycle: +1 goes FINER, -1 COARSER, and both CLAMP rather than wrapping. The finest division is
    // now 1/128, so that is where a held key parks.
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapCycleNext, false));
    EXPECT_EQ(view.snap, Snap::HundredTwentyEighth) << "already at the finest division - clamped, never wrapped to Bar";

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapCyclePrev, false));
    EXPECT_EQ(view.snap, Snap::SixtyFourth);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapCyclePrev, false));
    EXPECT_EQ(view.snap, Snap::ThirtySecond);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapCycleNext, false));
    EXPECT_EQ(view.snap, Snap::SixtyFourth);

    // The grid is SHARED, not per-surface: which timeline surface has focus must not change where a
    // grid command lands.
    for (auto surface : {MainComponent::EditSurface::Graph, MainComponent::EditSurface::TimelineClips,
                         MainComponent::EditSurface::PianoRoll}) {
        mc.setEditSurfaceOverrideForTest(surface);
        ASSERT_TRUE(cm.invokeDirectly(AppCommands::snapSetQuarter, false)) << "surface " << (int)surface;
        EXPECT_EQ(view.snap, Snap::Quarter) << "surface " << (int)surface;
    }
}

// ============================================================================
// 11. Zoom commands — routed by resolveEditSurface(), like the clipboard verbs
// ============================================================================

TEST_F(FocusArbitrationTest, ZoomCommandsRoutePerFocusedSurface) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1200, 800);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isBottomDockConfiguredVisible());

    auto& cm = mc.getCommandManager();
    auto& panel = mc.getTimelinePanel();
    auto& roll = panel.getPianoRoll();

    // ---- Piano roll: its OWN mapping, on both axes ----
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);
    const double rollBeatsBefore = roll.getPixelsPerBeat();
    const double rollSemisBefore = roll.getPixelsPerSemitone();
    const double panelBeatsUntouched = panel.getViewState().pixelsPerBeat;

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::zoomInHorizontal, false));
    EXPECT_GT(roll.getPixelsPerBeat(), rollBeatsBefore);
    EXPECT_DOUBLE_EQ(roll.getPixelsPerSemitone(), rollSemisBefore) << "horizontal zoom must not touch the row height";
    EXPECT_DOUBLE_EQ(panel.getViewState().pixelsPerBeat, panelBeatsUntouched)
        << "the roll has its own zoom - the panel's shared mapping must not move";

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::zoomInVertical, false));
    EXPECT_GT(roll.getPixelsPerSemitone(), rollSemisBefore);

    // In-then-out returns to where it started: the out factor is the exact reciprocal.
    const double afterIn = roll.getPixelsPerBeat();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::zoomOutHorizontal, false));
    EXPECT_LT(roll.getPixelsPerBeat(), afterIn);
    EXPECT_NEAR(roll.getPixelsPerBeat(), rollBeatsBefore, 1.0e-9);

    // ---- Clip lanes: the panel's shared view state ----
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    const double panelBeatsBefore = panel.getViewState().pixelsPerBeat;
    const double panelRowScaleBefore = panel.getViewState().rowHeightScale;
    const double rollUntouched = roll.getPixelsPerBeat();

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::zoomInHorizontal, false));
    EXPECT_GT(panel.getViewState().pixelsPerBeat, panelBeatsBefore);
    EXPECT_DOUBLE_EQ(roll.getPixelsPerBeat(), rollUntouched);

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::zoomInVertical, false));
    EXPECT_GT(panel.getViewState().rowHeightScale, panelRowScaleBefore);

    // ---- Graph: ONE uniform zoom, so the horizontal pair drives it and the vertical pair is
    // reported inactive rather than silently doing the same thing under a second key ----
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);
    const float canvasWidthBefore = mc.getGraphEditor().getVisibleCanvasRect().getWidth();
    ASSERT_GT(canvasWidthBefore, 0.0f) << "precondition: the canvas has real bounds";

    EXPECT_TRUE(commandIsActive(mc, AppCommands::zoomInHorizontal));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::zoomInVertical)) << "the canvas has no second axis";
    EXPECT_FALSE(commandIsActive(mc, AppCommands::zoomOutVertical));

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::zoomInHorizontal, false));
    EXPECT_LT(mc.getGraphEditor().getVisibleCanvasRect().getWidth(), canvasWidthBefore)
        << "zooming in shows LESS canvas";

    EXPECT_FALSE(cm.invokeDirectly(AppCommands::zoomInVertical, false));
}

// ============================================================================
// 12. The whole block is inactive while the timeline is hidden
// ============================================================================

TEST_F(FocusArbitrationTest, GridAndTimelineZoomCommandsAreInactiveWhileThePanelIsHidden) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1200, 800);
    ASSERT_FALSE(mc.isBottomDockConfiguredVisible()) << "the panel starts hidden by default";

    auto& cm = mc.getCommandManager();
    auto& view = mc.getTimelinePanel().getViewState();
    const auto snapBefore = view.snap;

    for (auto cmdId :
         {AppCommands::snapSetWhole, AppCommands::snapSetHalf, AppCommands::snapSetQuarter, AppCommands::snapSetEighth,
          AppCommands::snapSetSixteenth, AppCommands::snapSetThirtySecond, AppCommands::snapSetSixtyFourth,
          AppCommands::snapSetHundredTwentyEighth, AppCommands::snapCyclePrev, AppCommands::snapCycleNext}) {
        EXPECT_FALSE(commandIsActive(mc, cmdId)) << "command " << (int)cmdId;
        EXPECT_FALSE(cm.invokeDirectly(cmdId, false)) << "command " << (int)cmdId;
    }
    EXPECT_EQ(view.snap, snapBefore) << "a refused command must not have persisted a grid change";

    // Zoom follows the surface, so with the panel hidden resolveEditSurface() is Graph: the
    // horizontal pair stays live (it zooms the canvas) and the vertical pair does not.
    ASSERT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph);
    EXPECT_TRUE(commandIsActive(mc, AppCommands::zoomInHorizontal));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::zoomInVertical));

    // ...and a stale focus override cannot revive them either — the same "hidden panel never owns
    // the verbs" rule getCommandInfo applies to the clipboard verbs.
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    EXPECT_FALSE(commandIsActive(mc, AppCommands::zoomInHorizontal));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::zoomInVertical));
}

// ============================================================================
// 13. "Natural scrolling" — the Preferences toggle reaches both surfaces live
//
// The propagation path has no direct wire: the tab writes the settings key, juce::PropertiesFile
// broadcasts the change, and MainComponent's changeListenerCallback re-reads it. This drives the
// REAL chain (tab -> file -> listener) rather than calling the applier, because the wire is the part
// that can break — a missing addChangeListener, or a changeListenerCallback that stopped
// dispatching on the source, would both leave the applier itself perfectly correct.
// ============================================================================

TEST_F(FocusArbitrationTest, NaturalScrollingPreferenceReachesTheTimelineAndTheRoll) {
    PersistedKeysGuard guard({MainComponent::kNaturalScrollingKey});

    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1200, 800);

    // Default ON (natural) means NOT inverted on either surface — the juce::Viewport convention the
    // rest of the app already follows.
    mc.getAppPropertiesForTest().getUserSettings()->removeValue(MainComponent::kNaturalScrollingKey);
    mc.applyNaturalScrollingPreference();
    EXPECT_FALSE(mc.getTimelinePanel().isScrollInverted());
    EXPECT_FALSE(mc.getTimelinePanel().getPianoRoll().isScrollInverted());

    // The live path: a tab built on the SAME juce::ApplicationProperties instance MainComponent
    // listens to (which is exactly what SettingsWindow hands it).
    PreferencesSettingsTab prefs(mc.getAppPropertiesForTest());
    EXPECT_TRUE(prefs.isNaturalScrollingEnabled()) << "default ON";

    prefs.setNaturalScrollingEnabled(false);
    // ChangeBroadcaster posts its notification, so the loop has to turn once — the same idiom the
    // triggerClick() tests above use.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_TRUE(mc.getTimelinePanel().isScrollInverted());
    EXPECT_TRUE(mc.getTimelinePanel().getPianoRoll().isScrollInverted());

    prefs.setNaturalScrollingEnabled(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_FALSE(mc.getTimelinePanel().isScrollInverted());
    EXPECT_FALSE(mc.getTimelinePanel().getPianoRoll().isScrollInverted());
}

// ============================================================================
// 14. "Scroll up to zoom in" — the same settings-file path, the other wheel flag
// ============================================================================

TEST_F(FocusArbitrationTest, ZoomScrollPreferenceReachesTheTimelineAndTheRoll) {
    // BOTH wheel keys are guarded, not just the one under test: the independence assertion below
    // reads the plain-scroll flag, and that would otherwise report whatever this developer's real
    // settings file happens to say (the documented local-vs-CI trap this file's guard exists for).
    PersistedKeysGuard guard({MainComponent::kZoomScrollUpZoomsInKey, MainComponent::kNaturalScrollingKey});

    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1200, 800);

    // Default ON ("up zooms in") means NOT inverted — what both wheel-zoom surfaces already did
    // before the preference existed.
    mc.getAppPropertiesForTest().getUserSettings()->removeValue(MainComponent::kZoomScrollUpZoomsInKey);
    mc.getAppPropertiesForTest().getUserSettings()->removeValue(MainComponent::kNaturalScrollingKey);
    mc.applyNaturalScrollingPreference();
    mc.applyZoomScrollPreference();
    EXPECT_FALSE(mc.getTimelinePanel().isZoomScrollInverted());
    EXPECT_FALSE(mc.getTimelinePanel().getPianoRoll().isZoomScrollInverted())
        << "the panel forwards to the roll - one writer, two surfaces";

    PreferencesSettingsTab prefs(mc.getAppPropertiesForTest());
    EXPECT_TRUE(prefs.isZoomScrollUpZoomsInEnabled()) << "default ON";

    prefs.setZoomScrollUpZoomsInEnabled(false);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_TRUE(mc.getTimelinePanel().isZoomScrollInverted());
    EXPECT_TRUE(mc.getTimelinePanel().getPianoRoll().isZoomScrollInverted());
    // The two wheel preferences are independent: inverting the zoom must not touch plain scrolling.
    EXPECT_FALSE(mc.getTimelinePanel().isScrollInverted());

    prefs.setZoomScrollUpZoomsInEnabled(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_FALSE(mc.getTimelinePanel().isZoomScrollInverted());
    EXPECT_FALSE(mc.getTimelinePanel().getPianoRoll().isZoomScrollInverted());
}
