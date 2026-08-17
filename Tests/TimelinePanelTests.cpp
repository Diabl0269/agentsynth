// TimelinePanelTests.cpp
//
// Bottom-docked timeline panel shell + toolbar toggle + rebindable shortcut + slide animation.
//
// Four groups of coverage:
//   1. synth::ui::TimelinePanelComponent in isolation — pure layout/paint, no MainComponent, no
//      SYNTH_ENABLE_TIMELINE gating (the component itself always compiles; only MainComponent's
//      use of it is gated).
//   2. MainComponent integration — toggle, persistence, carve geometry. Gated
//      #if SYNTH_ENABLE_TIMELINE because the toggle button/command/carve compile out entirely
//      when the flag is OFF (see MainComponent.h/.cpp). HiddenByDefaultAndCarvesNothing is the
//      exception: it asserts the flag-OFF invariant too (nothing timeline-related visible or
//      carved), so it is deliberately NOT gated.
//   3. Ruler/grid/zoom/scroll/snap/loop-brace — synth::ui::TimelineRulerComponent and the panel's
//      wheel handling and snap combo, driven with a raw synth::TransportService (never through
//      MainComponent/AudioEngine), so — same reasoning as group 1 — none of it is gated either.
//   4. Track headers + the app-level timeline wiring MainComponent owns (publish-on-mutation, the
//      compound add-track flow, reconciliation after an undo/redo restore, .agsproj round trips,
//      recorder wiring). The panel-level header tests are ungated like groups 1–3; everything that
//      needs MainComponent's wiring is #if SYNTH_ENABLE_TIMELINE, since that wiring compiles out.
//      The header ROW itself is covered separately, against a stub host, in
//      TimelineTrackHeaderTests.cpp.
//   5. The resizable panel height — the panel's top-edge grab strip (ungated, panel level) and
//      MainComponent's ownership of the value: default from the theme metric, clamp, live relayout,
//      persistence (gated, like the rest of group 2).
//   6. Authoring gestures reaching the panel — a double-click on empty MIDI lane space creates a
//      clip and opens the piano roll on it (ungated, panel level).

#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIStateMapper.h"
#include "../Source/ProjectBundle.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Transport/TransportService.h"
#include "../Source/UI/TimelinePanelComponent.h"
#include "../Source/UI/TrackColour.h"
#include "../Source/UserSettings.h"
#include "MainComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// Mock AI provider — same minimal pattern as MainComponentTests.cpp's MockProvider.
// ============================================================================
class MockProviderTL : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockTL"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }

private:
    juce::String model = "MockModel";
};

// ============================================================================
// 1. synth::ui::TimelinePanelComponent — pure layout/paint smoke tests.
// ============================================================================

TEST(TimelinePanelComponentTest, PanelRegionsTile) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);

    const auto transport = panel.getTransportBarBounds();
    const auto trackHeader = panel.getTrackHeaderBounds();
    const auto lanes = panel.getLanesBounds();
    const auto full = panel.getLocalBounds();

    // Union (bounding box) covers the whole panel with no gaps at the edges.
    EXPECT_EQ(transport.getUnion(trackHeader).getUnion(lanes), full);

    // No overlap: the three regions' areas sum to exactly the panel's total area. Combined with
    // the union check above, this proves an exact tiling (three disjoint rects derived from
    // sequential removeFromTop/removeFromLeft calls, per resized()).
    const juce::int64 sumAreas = (juce::int64)transport.getWidth() * transport.getHeight() +
                                 (juce::int64)trackHeader.getWidth() * trackHeader.getHeight() +
                                 (juce::int64)lanes.getWidth() * lanes.getHeight();
    EXPECT_EQ(sumAreas, (juce::int64)full.getWidth() * full.getHeight());

    // Sanity on placement: transport is the top strip, trackHeader the left column of the
    // remainder, lanes the rest.
    EXPECT_EQ(transport.getY(), 0);
    EXPECT_EQ(trackHeader.getX(), 0);
    EXPECT_EQ(trackHeader.getY(), transport.getBottom());
    EXPECT_EQ(lanes.getX(), trackHeader.getRight());
    EXPECT_EQ(lanes.getY(), transport.getBottom());
    EXPECT_EQ(lanes.getRight(), full.getRight());
    EXPECT_EQ(lanes.getBottom(), full.getBottom());
}

TEST(TimelinePanelComponentTest, SnapshotSmoke) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);

    const juce::Image img = panel.createComponentSnapshot(panel.getLocalBounds());
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.getWidth(), 1200);
    EXPECT_EQ(img.getHeight(), 220);
}

// ============================================================================
// 2. MainComponent integration.
// ============================================================================

class TimelinePanelIntegrationTest : public ::testing::Test {
protected:
    // Same pattern as MainComponentTests.cpp / PanelAnimationAndLoadingTests.cpp: the delegating
    // MainComponent ctor reads/writes the shared on-disk "Agent Synth" ApplicationProperties, so
    // panel-visibility keys are reset to their documented defaults before AND after every test to
    // keep persistence tests hermetic regardless of execution order.
    void resetPanelKeys() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1"); // default: visible
            s->setValue("aiPanelVisible", "0");        // default: hidden
            s->setValue("minimapVisible", "1");        // default: visible
            s->setValue("timelinePanelVisible", "0");  // default: hidden
            // Removed, not defaulted: absent is what makes the theme metric the default height.
            s->removeValue(MainComponent::kTimelinePanelHeightKey);
            s->saveIfNeeded();
        }
    }

    void SetUp() override { resetPanelKeys(); }
    void TearDown() override { resetPanelKeys(); }
};

TEST_F(TimelinePanelIntegrationTest, HiddenByDefaultAndCarvesNothing) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);

    EXPECT_FALSE(mc.isTimelineConfiguredVisible());
    EXPECT_FALSE(mc.getTimelinePanel().isVisible());
    // No carve: the graph editor still reaches all the way down to the status bar.
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), mc.getStatusBar().getBounds().getY());
}

#if SYNTH_ENABLE_TIMELINE

TEST_F(TimelinePanelIntegrationTest, ToggleCarvesFullWidthAboveStatusBar) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);

    const int libraryX = mc.getGraphEditor().getBounds().getX();
    const int graphRight = mc.getGraphEditor().getBounds().getRight();

    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());
    ASSERT_TRUE(mc.getTimelinePanel().isVisible());

    const auto panelBounds = mc.getTimelinePanel().getBounds();
    EXPECT_EQ(panelBounds.getX(), 0);
    EXPECT_EQ(panelBounds.getWidth(), 1600);
    EXPECT_EQ(panelBounds.getHeight(), 220); // Metrics::timelinePanelHeight literal default
    // Sits directly above the status bar.
    EXPECT_EQ(panelBounds.getBottom(), mc.getStatusBar().getBounds().getY());

    // Graph editor shrunk by exactly the panel height.
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), panelBounds.getY());

    // Library/AI panels unaffected horizontally (default: library visible at 200px, AI hidden).
    EXPECT_EQ(mc.getGraphEditor().getBounds().getX(), libraryX);
    EXPECT_EQ(mc.getGraphEditor().getBounds().getRight(), graphRight);
}

TEST_F(TimelinePanelIntegrationTest, ToggleBackRestores) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    const auto initialBounds = mc.getGraphEditor().getBounds();

    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());

    mc.simulateToggleTimelineClick();
    EXPECT_FALSE(mc.isTimelineConfiguredVisible());
    EXPECT_FALSE(mc.getTimelinePanel().isVisible());
    EXPECT_EQ(mc.getGraphEditor().getBounds(), initialBounds);
}

TEST_F(TimelinePanelIntegrationTest, VisibilityPersists) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    ASSERT_FALSE(mc.isTimelineConfiguredVisible());

    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());
    EXPECT_TRUE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("timelinePanelVisible", false));

    // A second MainComponent reads the same on-disk properties file — visible from startup.
    MainComponent mc2(std::make_unique<MockProviderTL>());
    EXPECT_TRUE(mc2.isTimelineConfiguredVisible());
    EXPECT_TRUE(mc2.getTimelinePanel().isVisible());
}

#endif // SYNTH_ENABLE_TIMELINE

// ============================================================================
// 3. Ruler/grid/zoom/scroll/snap/loop-brace.
// ============================================================================

namespace {

// Hand-built MouseEvent, same pattern as GraphEditorTests.cpp/MinimapComponentTests.cpp — no OS
// mouse source exists headlessly, but MouseInputSource is copyable and Desktop always exposes
// one. mouseWasDragged is the constructor's own bool (JUCE stores it verbatim, see
// MouseEvent::mouseWasDraggedSinceMouseDown()) rather than anything derived from real mouse
// motion, so it is fully under the caller's control here.
juce::MouseEvent makeTimelineMouseEvent(juce::Component& comp, juce::Point<float> position, juce::ModifierKeys mods,
                                        bool mouseWasDragged, juce::Point<float> mouseDownPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos,
                            juce::Time::getCurrentTime(), 1, mouseWasDragged);
}

juce::MouseEvent makeClickEvent(juce::Component& comp, juce::Point<float> position,
                                juce::ModifierKeys mods = juce::ModifierKeys()) {
    return makeTimelineMouseEvent(comp, position, mods, false, position);
}

juce::MouseEvent makeDragEvent(juce::Component& comp, juce::Point<float> position, juce::Point<float> anchorPos,
                               juce::ModifierKeys mods = juce::ModifierKeys()) {
    return makeTimelineMouseEvent(comp, position, mods, true, anchorPos);
}

// The ruler is 24 px tall in every test below, so its zone split sits at y = 12: y < 12 is the loop
// (top) zone, y >= 12 the playhead (bottom) zone.
constexpr float kLoopZoneY = 5.0f;
constexpr float kPlayheadZoneY = 18.0f;

} // namespace

// A press in the playhead zone seeks immediately — before mouseUp, so the cursor lands under the
// finger rather than waiting for the release.
TEST(TimelineRulerInteractionTest, PressInPlayheadZoneSeeksSnapped) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    state.snap = synth::ui::TimelineViewState::Snap::Beat;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    // x = 180px -> beat 180/40 = 4.5 -> Beat-snapped (ties round up) to 5.0.
    const juce::Point<float> pos(180.0f, kPlayheadZoneY);
    ruler.mouseDown(makeClickEvent(ruler, pos));
    transport.tick(512); // drains the posted locateBeat() command

    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 5.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 1);

    // The release adds nothing new: same snapped beat, so the throttle suppresses a second post.
    ruler.mouseUp(makeClickEvent(ruler, pos));
    transport.tick(512);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 5.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 1);
}

// Dragging in the playhead zone scrubs: the position follows the pointer, and a locateBeat() is
// posted only when the snapped beat actually changes.
TEST(TimelineRulerInteractionTest, DragInPlayheadZoneScrubsAndThrottlesPosts) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    state.snap = synth::ui::TimelineViewState::Snap::Beat;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    const juce::Point<float> pressPos(100.0f, kPlayheadZoneY); // beat 2.5 -> snapped 3.0
    ruler.mouseDown(makeClickEvent(ruler, pressPos));
    transport.tick(512);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 3.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 1);

    // Two moves that stay inside the same snapped beat (2.625 and 3.125 both snap to 3.0) post
    // nothing at all.
    ruler.mouseDrag(makeDragEvent(ruler, {105.0f, kPlayheadZoneY}, pressPos));
    ruler.mouseDrag(makeDragEvent(ruler, {125.0f, kPlayheadZoneY}, pressPos));
    transport.tick(512);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 3.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 1);

    ruler.mouseDrag(makeDragEvent(ruler, {140.0f, kPlayheadZoneY}, pressPos)); // beat 3.5 -> 4.0
    transport.tick(512);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 4.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 2);

    ruler.mouseDrag(makeDragEvent(ruler, {180.0f, kPlayheadZoneY}, pressPos)); // beat 4.5 -> 5.0
    ruler.mouseUp(makeDragEvent(ruler, {180.0f, kPlayheadZoneY}, pressPos));
    transport.tick(512);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 5.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 3);

    // Scrubbing never touches the loop.
    EXPECT_FALSE(transport.getPositionSnapshot().looping);
}

// The zone is latched at mouseDown: a scrub that wanders up into the loop zone keeps scrubbing and
// never posts a loop.
TEST(TimelineRulerInteractionTest, GestureZoneIsStickyForTheWholeDrag) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    state.snap = synth::ui::TimelineViewState::Snap::Beat;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    ASSERT_TRUE(transport.setLoop(2.0, 6.0, true));
    transport.tick(512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    const juce::Point<float> pressPos(100.0f, kPlayheadZoneY);
    ruler.mouseDown(makeClickEvent(ruler, pressPos));
    EXPECT_EQ(ruler.getGestureZoneForTest(), synth::ui::TimelineRulerComponent::Zone::Playhead);

    // Same x travel, but the pointer has left the bottom band — and even the strip's top edge.
    ruler.mouseDrag(makeDragEvent(ruler, {180.0f, kLoopZoneY}, pressPos));
    ruler.mouseDrag(makeDragEvent(ruler, {180.0f, -20.0f}, pressPos));
    ruler.mouseUp(makeDragEvent(ruler, {180.0f, -20.0f}, pressPos));
    transport.tick(512);

    const auto snap = transport.getPositionSnapshot();
    EXPECT_DOUBLE_EQ(snap.ppq, 5.0);
    EXPECT_EQ(ruler.getGestureZoneForTest(), synth::ui::TimelineRulerComponent::Zone::Playhead);
    // Loop untouched.
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);
}

// A click with no drag in the loop zone is inert: neither the loop nor the position moves. This is
// the whole point of the split — a stray click on the brace must not clear it.
TEST(TimelineRulerInteractionTest, ClickInLoopZoneChangesNothing) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    state.snap = synth::ui::TimelineViewState::Snap::Beat;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    ASSERT_TRUE(transport.setLoop(2.0, 6.0, true));
    ASSERT_TRUE(transport.locateBeat(1.0));
    transport.tick(512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    const juce::Point<float> pos(300.0f, kLoopZoneY);
    ruler.mouseDown(makeClickEvent(ruler, pos));
    ruler.mouseUp(makeClickEvent(ruler, pos));
    transport.tick(512);

    const auto snap = transport.getPositionSnapshot();
    EXPECT_EQ(ruler.getGestureZoneForTest(), synth::ui::TimelineRulerComponent::Zone::Loop);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 0);
    EXPECT_DOUBLE_EQ(snap.ppq, 1.0);
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);
}

// Cmd+click (no drag) toggles looping off, keeping the prior bounds — from either zone.
TEST(TimelineRulerInteractionTest, CommandClickTogglesLoopOffKeepingBounds) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    const juce::ModifierKeys cmd(juce::ModifierKeys::commandModifier);
    for (const float y : {kLoopZoneY, kPlayheadZoneY}) {
        ASSERT_TRUE(transport.setLoop(2.0, 6.0, true));
        transport.tick(512);
        ASSERT_TRUE(transport.getPositionSnapshot().looping);

        const juce::Point<float> pos(100.0f, y);
        ruler.mouseDown(makeClickEvent(ruler, pos, cmd));
        ruler.mouseUp(makeClickEvent(ruler, pos, cmd));
        transport.tick(512);

        const auto snap = transport.getPositionSnapshot();
        EXPECT_FALSE(snap.looping) << "y = " << y;
        EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
        EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);
    }
    // Cmd never seeks, in either zone.
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 0);
}

// Press-drag-release in the loop zone sets the loop to the snapped [min,max] range; dragging
// leftwards (releasing before the press point) must still normalise start < end.
TEST(TimelineRulerInteractionTest, DragSetsLoopNormalisingReversedDrag) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 20.0;
    state.snap = synth::ui::TimelineViewState::Snap::Bar; // default 4/4 -> bars at beat 0,4,8,...

    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    // Press at x=170 (beat 8.5 -> bar-snapped 8), drag/release at x=50 (beat 2.5 -> bar-snapped 4).
    const juce::Point<float> pressPos(170.0f, kLoopZoneY);
    const juce::Point<float> releasePos(50.0f, kLoopZoneY);

    ruler.mouseDown(makeClickEvent(ruler, pressPos));
    ruler.mouseDrag(makeDragEvent(ruler, releasePos, pressPos));
    ruler.mouseUp(makeDragEvent(ruler, releasePos, pressPos));

    transport.tick(512);

    const auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 4.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 8.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 0); // a loop drag never moves the playhead
}

// Hover indicator: tracks the pointer's half, clears on exit. The boundary row (y == height/2)
// belongs to the playhead zone.
TEST(TimelineRulerInteractionTest, HoveredZoneFollowsPointerHalfAndClearsOnExit) {
    using Zone = synth::ui::TimelineRulerComponent::Zone;

    synth::ui::TimelineViewState state;
    synth::ui::TimelineRulerComponent ruler(state); // no transport: hover is pure affordance
    ruler.setSize(800, 24);

    EXPECT_FALSE(ruler.getHoveredZoneForTest().has_value());

    ruler.mouseEnter(makeClickEvent(ruler, {100.0f, kLoopZoneY}));
    ASSERT_TRUE(ruler.getHoveredZoneForTest().has_value());
    EXPECT_EQ(*ruler.getHoveredZoneForTest(), Zone::Loop);

    ruler.mouseMove(makeClickEvent(ruler, {400.0f, kLoopZoneY})); // same zone, different x
    EXPECT_EQ(*ruler.getHoveredZoneForTest(), Zone::Loop);

    ruler.mouseMove(makeClickEvent(ruler, {400.0f, 12.0f})); // exactly on the split
    EXPECT_EQ(*ruler.getHoveredZoneForTest(), Zone::Playhead);

    ruler.mouseMove(makeClickEvent(ruler, {400.0f, kPlayheadZoneY}));
    EXPECT_EQ(*ruler.getHoveredZoneForTest(), Zone::Playhead);

    ruler.mouseExit(makeClickEvent(ruler, {400.0f, 40.0f}));
    EXPECT_FALSE(ruler.getHoveredZoneForTest().has_value());
}

// Plain wheel scrolls (pixelsPerBeat untouched); Cmd+wheel zooms around the cursor, keeping the
// beat under it fixed.
TEST(TimelinePanelInteractionTest, WheelScrollsAndCmdWheelZooms) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);

    auto& state = panel.getViewState();
    // Start comfortably away from the firstVisibleBeat >= 0 clamp so the scroll below is visible
    // regardless of which wheel direction maps to which scroll direction.
    state.firstVisibleBeat = 500.0;
    const double ppbBefore = state.pixelsPerBeat;
    const double firstVisibleBefore = state.firstVisibleBeat;

    juce::MouseWheelDetails wheel;
    wheel.deltaY = 0.5f;
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}), wheel);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, ppbBefore);
    EXPECT_NE(state.firstVisibleBeat, firstVisibleBefore);

    const juce::Point<float> cursor(300.0f, 12.0f);
    // The ruler shares TimelineViewState's x==0 origin, so reproject into its coordinate space to
    // compute the same anchor beat the panel's mouseWheelMove uses internally.
    const double anchorXInRuler = (double)cursor.x - (double)panel.getRuler().getX();
    const double anchorBeatBefore = state.xToBeat(anchorXInRuler);
    const double ppbBeforeZoom = state.pixelsPerBeat;

    juce::MouseWheelDetails zoomWheel;
    zoomWheel.deltaY = 0.5f;
    panel.mouseWheelMove(makeClickEvent(panel, cursor, juce::ModifierKeys(juce::ModifierKeys::commandModifier)),
                         zoomWheel);

    EXPECT_NE(state.pixelsPerBeat, ppbBeforeZoom);
    EXPECT_GE(state.pixelsPerBeat, synth::ui::TimelineViewState::kMinPixelsPerBeat);
    EXPECT_LE(state.pixelsPerBeat, synth::ui::TimelineViewState::kMaxPixelsPerBeat);
    EXPECT_NEAR(state.xToBeat(anchorXInRuler), anchorBeatBefore, 1e-6);
}

// The snap combo's choice persists to ApplicationProperties under "timelineSnap" and is restored
// by a freshly-constructed panel reading the same (isolated, test-only) properties file.
TEST(TimelinePanelSnapComboTest, SnapChoicePersists) {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth Timeline Snap Test";
    opts.folderName = "Agent Synth Timeline Snap Test";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;

    // Hermetic regardless of a previous run: start from no file at all.
    {
        juce::ApplicationProperties initial;
        initial.setStorageParameters(opts);
        if (auto* s = initial.getUserSettings())
            s->getFile().deleteFile();
    }

    juce::ApplicationProperties props;
    props.setStorageParameters(opts);

    synth::ui::TimelinePanelComponent panel;
    panel.setApplicationProperties(&props);
    ASSERT_EQ(panel.getViewState().snap, synth::ui::TimelineViewState::Snap::Beat); // documented default

    panel.getSnapCombo().setSelectedId(7, juce::sendNotificationSync); // "1/16" -> Sixteenth
    EXPECT_EQ(panel.getViewState().snap, synth::ui::TimelineViewState::Snap::Sixteenth);

    juce::ApplicationProperties props2;
    props2.setStorageParameters(opts);
    synth::ui::TimelinePanelComponent panel2;
    panel2.setApplicationProperties(&props2);
    EXPECT_EQ(panel2.getViewState().snap, synth::ui::TimelineViewState::Snap::Sixteenth);
    EXPECT_EQ(panel2.getSnapCombo().getSelectedId(), 7);

    if (auto* s = props.getUserSettings())
        s->getFile().deleteFile();
}

// ============================================================================
// 4. Track headers + the app-level timeline wiring.
// ============================================================================

namespace {

using synth::TrackKind;
using synth::ui::TimelineTrackHeaderComponent;

juce::AudioProcessorGraph::Node* findNodeOfType(juce::AudioProcessorGraph& graph, ModuleType type) {
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            if (module->getModuleType() == type)
                return node;
    }
    return nullptr;
}

int countNodesOfType(juce::AudioProcessorGraph& graph, ModuleType type) {
    int count = 0;
    for (auto* node : graph.getNodes())
        if (node != nullptr)
            if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
                if (module->getModuleType() == type)
                    ++count;
    return count;
}

int countMidiConnections(juce::AudioProcessorGraph& graph) {
    int count = 0;
    for (const auto& connection : graph.getConnections())
        if (connection.source.isMIDI())
            ++count;
    return count;
}

bool hasMidiConnection(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID from,
                       juce::AudioProcessorGraph::NodeID to) {
    for (const auto& connection : graph.getConnections())
        if (connection.source.isMIDI() && connection.source.nodeID == from && connection.destination.nodeID == to)
            return true;
    return false;
}

} // namespace

// ---- Panel level (ungated): the header column is a pure view of the document ----

TEST(TimelinePanelTrackHeaderTest, HeadersFollowTheDocumentWithNoTimer) {
    synth::TimelineDoc doc; // declared first: the panel removes its listener in its destructor
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    panel.setTimelineDoc(&doc);
    EXPECT_EQ(panel.getTrackHeaderCount(), 0);

    const auto first = doc.addTrack(TrackKind::Midi, "Track 1");
    EXPECT_EQ(panel.getTrackHeaderCount(), 1);
    doc.addTrack(TrackKind::Midi, "Track 2");
    ASSERT_EQ(panel.getTrackHeaderCount(), 2);

    // A value change REFRESHES the existing rows; it must not rebuild them (that would drop an
    // in-progress name edit and churn the whole column on every mute click).
    auto* firstHeader = panel.getTrackHeaderAt(0);
    ASSERT_NE(firstHeader, nullptr);
    doc.setTrackName(first, "Bassline");
    EXPECT_EQ(panel.getTrackHeaderAt(0), firstHeader);
    EXPECT_EQ(firstHeader->getNameLabel().getText(), "Bassline");

    doc.removeTrack(first);
    EXPECT_EQ(panel.getTrackHeaderCount(), 1);
    EXPECT_EQ(panel.getTrackHeaderAt(0)->getNameLabel().getText(), "Track 2");

    panel.setTimelineDoc(nullptr);
    EXPECT_EQ(panel.getTrackHeaderCount(), 0);
}

TEST(TimelinePanelTrackHeaderTest, AddButtonAndHeaderListStayInsideTheHeaderColumn) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    panel.setTimelineDoc(&doc);

    const auto column = panel.getTrackHeaderBounds();
    EXPECT_TRUE(column.contains(panel.getAddTrackButton().getBounds()));
    EXPECT_TRUE(column.contains(panel.getTrackHeaderViewport().getBounds()));
    // The button sits above the scrolling list, and the two do not overlap.
    EXPECT_LE(panel.getAddTrackButton().getBounds().getBottom(), panel.getTrackHeaderViewport().getY());
}

TEST(TimelinePanelTrackHeaderTest, HeaderListScrollsWhenTracksOverflow) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    panel.setTimelineDoc(&doc);

    for (int i = 0; i < 12; ++i)
        doc.addTrack(TrackKind::Midi, "Track " + juce::String(i + 1));

    ASSERT_EQ(panel.getTrackHeaderCount(), 12);
    auto& viewport = panel.getTrackHeaderViewport();
    ASSERT_NE(viewport.getViewedComponent(), nullptr);
    EXPECT_GT(viewport.getViewedComponent()->getHeight(), viewport.getMaximumVisibleHeight())
        << "12 rows must overflow a 220 px panel — that is what the Viewport is for";
    EXPECT_TRUE(viewport.isVerticalScrollBarShown());
}

#if SYNTH_ENABLE_TIMELINE

// MainComponent owns the doc, the recorder and the reconcile hooks — all of which compile out with
// the flag off, hence the gate (same rule as group 2 above).
class TimelineAppWiringTest : public TimelinePanelIntegrationTest {
protected:
    // An empty canvas plus `polyMidiCount` Poly MIDI modules, and a clean undo stack, so a test's
    // first undo step is the one it just took.
    static void prepareCanvas(MainComponent& mc, int polyMidiCount) {
        mc.getGraphEditor().newPatch();
        for (int i = 0; i < polyMidiCount; ++i)
            mc.getGraphEditor().addModuleAtCanvasPosition("Poly MIDI", {200 + i * 320, 200}, {});
        mc.getUndoManager().clearUndoHistory();
    }

    // Nothing in these tests wants a real device clocking the graph underneath them: the transport
    // is ticked by hand, and the timeline snapshot exchange is read from this thread.
    static void quiesceEngine(MainComponent& mc) { mc.getAudioEngine().suspendDeviceCallback(); }
};

// ---- 1. Publish-on-mutation ----

TEST_F(TimelineAppWiringTest, PublishOnMutation) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    quiesceEngine(mc);

    auto& engine = mc.getAudioEngine();
    EXPECT_EQ(engine.getTimelineSnapshots().beginAudioBlock().tracks.size(), 0u);

    auto& doc = mc.getTimelineDoc();
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    ASSERT_TRUE(trackId.isValid());
    doc.setTrackBinding(trackId, "uuid-under-test");
    doc.setTrackMuted(trackId, true);

    // No explicit publish call anywhere above: the doc notified, and MainComponent republished.
    const auto& snapshot = engine.getTimelineSnapshots().beginAudioBlock();
    ASSERT_EQ(snapshot.tracks.size(), 1u);
    EXPECT_STREQ(snapshot.tracks[0].bindingUuid, "uuid-under-test");
    EXPECT_TRUE(snapshot.tracks[0].muted);

    doc.removeTrack(trackId);
    EXPECT_EQ(engine.getTimelineSnapshots().beginAudioBlock().tracks.size(), 0u);
}

// ---- 2. The add-track flow: one node, one track, one wire, ONE undo step ----

TEST_F(TimelineAppWiringTest, AddMidiTrackFlowCompoundUndo) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 1);

    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto* polyMidi = findNodeOfType(graph, ModuleType::PolyMidi);
    ASSERT_NE(polyMidi, nullptr);

    mc.simulateAddMidiTrackClick();

    // The graph half.
    ASSERT_EQ(countNodesOfType(graph, ModuleType::TimelineMidiSource), 1);
    auto* trackIn = findNodeOfType(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackIn, nullptr);
    const juce::String uuid = trackIn->properties["uuid"].toString();
    EXPECT_TRUE(uuid.isNotEmpty()) << "the flow must assign a uuid — the binding keys on it";
    if (auto* module = dynamic_cast<ModuleBase*>(trackIn->getProcessor()))
        EXPECT_EQ(juce::String(module->getNodeUuid()), uuid) << "and mirror it into the processor";
    EXPECT_TRUE(hasMidiConnection(graph, trackIn->nodeID, polyMidi->nodeID));

    // The timeline half.
    ASSERT_EQ(doc.getTracks().size(), 1u);
    EXPECT_EQ(doc.getTracks()[0].name, "Track 1");
    EXPECT_EQ(doc.getTracks()[0].bindingUuid, uuid);
    EXPECT_FALSE(doc.getTracks()[0].orphaned);
    EXPECT_EQ(doc.getTracks()[0].colourArgb, synth::ui::trackPaletteColour(0).getARGB());

    // The view half.
    ASSERT_EQ(mc.getTimelinePanel().getTrackHeaderCount(), 1);
    EXPECT_FALSE(mc.getTimelinePanel().getTrackHeaderAt(0)->isBindingChipWarning());

    // ONE undo takes all of it back.
    ASSERT_TRUE(mc.getUndoManager().canUndo());
    mc.getUndoManager().undo();
    EXPECT_TRUE(doc.isEmpty());
    EXPECT_EQ(countNodesOfType(graph, ModuleType::TimelineMidiSource), 0);
    EXPECT_EQ(countMidiConnections(graph), 0);
    EXPECT_EQ(mc.getTimelinePanel().getTrackHeaderCount(), 0);

    // ...and redo restores both halves, with the SAME uuid, so the binding still resolves.
    mc.getUndoManager().redo();
    ASSERT_EQ(doc.getTracks().size(), 1u);
    auto* restored = findNodeOfType(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->properties["uuid"].toString(), uuid);
    EXPECT_EQ(doc.getTracks()[0].bindingUuid, uuid);
    EXPECT_FALSE(doc.getTracks()[0].orphaned) << "the post-restore reconcile re-derived this";
}

TEST_F(TimelineAppWiringTest, AddTrackAtTheCapAddsNoNode) {
    // The doc refuses a track past kMaxTracks. The node has to be created AFTER that refusal is
    // known, or the graph keeps a Track In / Track Audio node no track will ever play through —
    // recordCombinedChange records the mutation, it does not roll one back.
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 1);

    auto& doc = mc.getTimelineDoc();
    auto& graph = mc.getAudioEngine().getGraph();
    while ((int)doc.getTracks().size() < synth::TimelineDoc::kMaxTracks)
        ASSERT_TRUE(doc.addTrack(TrackKind::Midi, "Filler").isValid());

    const int nodesBefore = graph.getNumNodes();

    mc.simulateAddMidiTrackClick();
    EXPECT_EQ((int)doc.getTracks().size(), synth::TimelineDoc::kMaxTracks);
    EXPECT_EQ(countNodesOfType(graph, ModuleType::TimelineMidiSource), 0)
        << "a refused MIDI track must leave no orphan Track In node";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);

    mc.simulateAddAudioTrackClick();
    EXPECT_EQ((int)doc.getTracks().size(), synth::TimelineDoc::kMaxTracks);
    EXPECT_EQ(countNodesOfType(graph, ModuleType::TimelineAudioSource), 0)
        << "a refused audio track must leave no orphan Track Audio node";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);

    // Nothing changed in either domain, so there is no undo step to take back either.
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}

// ---- 3. Auto-wire only when the target is unambiguous ----

TEST_F(TimelineAppWiringTest, AmbiguousTargetNoAutoWire) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 2);

    auto& graph = mc.getAudioEngine().getGraph();
    ASSERT_EQ(countNodesOfType(graph, ModuleType::PolyMidi), 2);
    ASSERT_EQ(countMidiConnections(graph), 0);

    mc.simulateAddMidiTrackClick();

    // The node and the track are still created and bound — only the CABLE is left to the user.
    EXPECT_EQ(countNodesOfType(graph, ModuleType::TimelineMidiSource), 1);
    ASSERT_EQ(mc.getTimelineDoc().getTracks().size(), 1u);
    EXPECT_TRUE(mc.getTimelineDoc().getTracks()[0].bindingUuid.isNotEmpty());
    EXPECT_EQ(countMidiConnections(graph), 0) << "two candidates: guessing one would be worse than no wire";
}

TEST_F(TimelineAppWiringTest, NoTargetNoAutoWire) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 0);

    mc.simulateAddMidiTrackClick();

    auto& graph = mc.getAudioEngine().getGraph();
    EXPECT_EQ(countNodesOfType(graph, ModuleType::TimelineMidiSource), 1);
    EXPECT_EQ(countMidiConnections(graph), 0);
    EXPECT_EQ(mc.getTimelineDoc().getTracks().size(), 1u);
}

// ---- 4. Binding chip states end to end, and the one-click re-bind ----

TEST_F(TimelineAppWiringTest, BindingChipGoesAmberWhenTheNodeIsDeletedAndRebindsInOneClick) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 1);
    mc.simulateAddMidiTrackClick();

    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& panel = mc.getTimelinePanel();
    auto* trackIn = findNodeOfType(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackIn, nullptr);
    const juce::String originalUuid = trackIn->properties["uuid"].toString();
    const auto trackInNodeId = trackIn->nodeID;

    ASSERT_EQ(panel.getTrackHeaderCount(), 1);
    EXPECT_TRUE(panel.getTrackHeaderAt(0)->getBindingChipText().startsWith("Track In"));

    // Delete the node the way the canvas does (recordStructuralChange — NOT an undo restore, so
    // this is the path the onGraphStructureChanged safety net exists for).
    mc.getGraphEditor().requestDeleteModule(trackInNodeId);

    ASSERT_EQ(doc.getTracks().size(), 1u);
    EXPECT_TRUE(doc.getTracks()[0].orphaned) << "reconciled with no explicit call from this test";
    EXPECT_EQ(doc.getTracks()[0].bindingUuid, originalUuid) << "an orphaned binding is retained";
    ASSERT_EQ(panel.getTrackHeaderCount(), 1);
    EXPECT_EQ(panel.getTrackHeaderAt(0)->getBindingChipText(), "Missing");
    EXPECT_TRUE(panel.getTrackHeaderAt(0)->isBindingChipWarning());

    // One click on the chip menu's "New Track In node" recovers it.
    panel.getTrackHeaderAt(0)->applyBindingMenuChoice(TimelineTrackHeaderComponent::kNewTrackInNodeMenuId);

    auto* replacement = findNodeOfType(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(replacement, nullptr);
    const juce::String newUuid = replacement->properties["uuid"].toString();
    EXPECT_NE(newUuid, originalUuid);
    EXPECT_EQ(doc.getTracks()[0].bindingUuid, newUuid);
    EXPECT_FALSE(doc.getTracks()[0].orphaned);
    EXPECT_FALSE(panel.getTrackHeaderAt(0)->isBindingChipWarning());
}

TEST_F(TimelineAppWiringTest, ChipMenuOffersOnlyUnclaimedTrackInNodes) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 1);

    mc.simulateAddMidiTrackClick();
    mc.simulateAddMidiTrackClick();

    auto& panel = mc.getTimelinePanel();
    ASSERT_EQ(panel.getTrackHeaderCount(), 2);
    ASSERT_EQ(countNodesOfType(mc.getAudioEngine().getGraph(), ModuleType::TimelineMidiSource), 2);

    // Two Track In nodes exist, but the OTHER track already claims one of them.
    const auto options = panel.getTrackHeaderAt(0)->collectBindingOptions();
    ASSERT_EQ(options.size(), 1u);
    EXPECT_EQ(options[0].uuid, mc.getTimelineDoc().getTracks()[0].bindingUuid)
        << "a track's own binding stays on its menu; another track's does not";
}

// ---- 5. A restore re-derives the orphan flags ----

TEST_F(TimelineAppWiringTest, UndoRestoreReconciles) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 1);
    mc.simulateAddMidiTrackClick();

    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto* trackIn = findNodeOfType(graph, ModuleType::TimelineMidiSource);
    ASSERT_NE(trackIn, nullptr);

    mc.getGraphEditor().requestDeleteModule(trackIn->nodeID);
    ASSERT_EQ(doc.getTracks().size(), 1u);
    ASSERT_TRUE(doc.getTracks()[0].orphaned);

    // Undo brings the node back. NOTHING in this test reconciles by hand — the AppUndoManager's
    // post-restore hook does it.
    mc.getUndoManager().undo();
    ASSERT_EQ(doc.getTracks().size(), 1u);
    EXPECT_FALSE(doc.getTracks()[0].orphaned);
    EXPECT_FALSE(mc.getTimelinePanel().getTrackHeaderAt(0)->isBindingChipWarning());
}

// ---- 6. New Patch empties the timeline too ----

TEST_F(TimelineAppWiringTest, NewPatchClearsTheTimeline) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);
    prepareCanvas(mc, 1);
    mc.simulateAddMidiTrackClick();
    ASSERT_EQ(mc.getTimelineDoc().getTracks().size(), 1u);

    // invokeDirectly(..., false) runs the command synchronously — the toolbar button posts it
    // asynchronously, which a headless test has no message loop to deliver.
    mc.getCommandManager().invokeDirectly(AppCommands::newPatch, false);

    EXPECT_TRUE(mc.getTimelineDoc().isEmpty());
    EXPECT_EQ(countNodesOfType(mc.getAudioEngine().getGraph(), ModuleType::TimelineMidiSource), 0);
    EXPECT_EQ(mc.getTimelinePanel().getTrackHeaderCount(), 0);
    EXPECT_EQ(mc.getAudioEngine().getTimelineSnapshots().beginAudioBlock().tracks.size(), 0u);
}

// ---- 7. .agsproj round trip, and .json unaffected ----

TEST_F(TimelineAppWiringTest, AgsprojRoundTripThroughMainComponent) {
    const juce::File scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("agentsynth-tl53-" + juce::Uuid().toDashedString());
    ASSERT_TRUE(scratch.createDirectory());
    const juce::File bundle = scratch.getChildFile("Project.agsproj");
    const juce::File preset = scratch.getChildFile("Preset.json");

    {
        MainComponent mc(std::make_unique<MockProviderTL>());
        mc.setSize(1600, 900);
        quiesceEngine(mc);
        prepareCanvas(mc, 1);
        mc.simulateAddMidiTrackClick();

        auto& doc = mc.getTimelineDoc();
        auto& graph = mc.getAudioEngine().getGraph();
        const juce::String savedUuid = doc.getTracks()[0].bindingUuid;

        mc.saveProjectForTest(bundle);
        ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle));

        // Mutate after saving — none of this may survive the reopen.
        doc.addTrack(TrackKind::Midi, "Scratch");
        mc.getGraphEditor().addModuleAtCanvasPosition("Reverb", {900, 500}, {});
        ASSERT_EQ(doc.getTracks().size(), 2u);

        ASSERT_TRUE(mc.openProjectForTest(bundle));

        ASSERT_EQ(doc.getTracks().size(), 1u);
        EXPECT_EQ(doc.getTracks()[0].bindingUuid, savedUuid);
        EXPECT_FALSE(doc.getTracks()[0].orphaned) << "reconciled against the reloaded graph";
        EXPECT_EQ(countNodesOfType(graph, ModuleType::Reverb), 0) << "the graph came back too";
        auto* reloaded = findNodeOfType(graph, ModuleType::TimelineMidiSource);
        ASSERT_NE(reloaded, nullptr);
        EXPECT_EQ(reloaded->properties["uuid"].toString(), savedUuid);

        // Published, and the header column rebuilt from the loaded doc.
        EXPECT_EQ(mc.getAudioEngine().getTimelineSnapshots().beginAudioBlock().tracks.size(), 1u);
        EXPECT_EQ(mc.getTimelinePanel().getTrackHeaderCount(), 1);

        // A plain .json save is untouched by any of this: no "timeline" key, and loading one back
        // leaves the live timeline exactly as it was.
        mc.saveProjectForTest(preset);
        ASSERT_TRUE(preset.existsAsFile());
        const juce::var json = juce::JSON::parse(preset);
        ASSERT_TRUE(json.isObject());
        EXPECT_FALSE(json.getDynamicObject()->hasProperty("timeline"));

        doc.addTrack(TrackKind::Midi, "Kept");
        ASSERT_EQ(doc.getTracks().size(), 2u);
        ASSERT_TRUE(mc.openProjectForTest(preset));
        EXPECT_EQ(doc.getTracks().size(), 2u) << "a .json preset carries no timeline, so it clears none";
    }

    scratch.deleteRecursively();
}

namespace {

/** Records the clip streamer's asset root at every doc notification. The one that matters fires
 *  from INSIDE ProjectBundle::load (fromVar moves the timeline into the live doc, which publishes
 *  synchronously) — long before the load returns. */
class AssetRootWatcher : public synth::TimelineDoc::Listener {
public:
    AssetRootWatcher(synth::TimelineDoc& doc, AudioEngine& engine)
        : doc_(doc)
        , engine_(engine) {
        doc_.addListener(this);
    }
    ~AssetRootWatcher() override { doc_.removeListener(this); }

    void timelineChanged(const synth::TimelineDoc&) override {
        if (notifications++ == 0)
            firstRoot = engine_.getAudioClipStreamer().getBundleRoot();
        lastRoot = engine_.getAudioClipStreamer().getBundleRoot();
    }

    juce::File firstRoot;
    juce::File lastRoot;
    int notifications = 0;

private:
    synth::TimelineDoc& doc_;
    AudioEngine& engine_;
};

/** A bundle with one audio track whose clip names `assetName`, and that file physically present in
 *  its Audio/ folder. Content is irrelevant — nothing here decodes it. */
void writeBundleWithClip(MainComponent& mc, const juce::File& bundleDir, const juce::String& assetName) {
    auto& doc = mc.getTimelineDoc();
    doc.clear();
    const auto trackId = doc.addTrack(TrackKind::Audio, "Audio 1");
    ASSERT_TRUE(trackId.isValid());
    const auto clipId = doc.addClip(trackId, 0.0, 4.0, assetName);
    ASSERT_TRUE(clipId.isValid());
    ASSERT_TRUE(doc.setClipAsset(clipId, "Audio/" + assetName, 0.0));

    mc.saveProjectForTest(bundleDir);
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundleDir));
    bundleDir.getChildFile(synth::ProjectBundle::kAudioSubdirName)
        .getChildFile(assetName)
        .replaceWithText("not really audio");
}

} // namespace

TEST_F(TimelineAppWiringTest, OpeningABundlePublishesAgainstItsOwnAssetRoots) {
    const juce::File scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("agentsynth-bundle-roots-" + juce::Uuid().toDashedString());
    ASSERT_TRUE(scratch.createDirectory());

    const juce::File bundleA = scratch.getChildFile("A.agsproj");
    const juce::File bundleB = scratch.getChildFile("B.agsproj");

    {
        MainComponent mc(std::make_unique<MockProviderTL>());
        mc.setSize(1600, 900);
        quiesceEngine(mc);
        prepareCanvas(mc, 0);

        writeBundleWithClip(mc, bundleA, "a.wav");
        writeBundleWithClip(mc, bundleB, "b.wav");

        auto& engine = mc.getAudioEngine();
        ASSERT_TRUE(mc.openProjectForTest(bundleA));
        ASSERT_EQ(engine.getAudioClipStreamer().getBundleRoot(), bundleA);

        // Opening B: the publish that fires from inside the load must already see B's root. With
        // the roots still on A, B's "Audio/b.wav" resolves inside A — the wrong file, or silence.
        {
            AssetRootWatcher watcher(mc.getTimelineDoc(), engine);
            ASSERT_TRUE(mc.openProjectForTest(bundleB));
            ASSERT_GT(watcher.notifications, 0) << "the load never notified, so this proves nothing";
            EXPECT_EQ(watcher.firstRoot, bundleB) << "the load published against the PREVIOUS bundle's roots";
        }
        EXPECT_EQ(engine.getAudioClipStreamer().getBundleRoot(), bundleB);
        EXPECT_EQ(engine.getAudioClipStreamer().resolveAssetRef("Audio/b.wav"),
                  bundleB.getChildFile("Audio").getChildFile("b.wav"));

        // A failed load is all-or-nothing, roots included: the still-open project stays on B.
        const juce::File corrupt = scratch.getChildFile("Corrupt.agsproj");
        ASSERT_TRUE(corrupt.createDirectory());
        corrupt.getChildFile(synth::ProjectBundle::kProjectFileName).replaceWithText("{ not json at all");

        const auto tracksBefore = mc.getTimelineDoc().getTracks().size();
        EXPECT_FALSE(mc.openProjectForTest(corrupt));
        EXPECT_EQ(engine.getAudioClipStreamer().getBundleRoot(), bundleB)
            << "a refused load must leave the previous project's asset roots in place";
        EXPECT_EQ(mc.getTimelineDoc().getTracks().size(), tracksBefore);
    }

    scratch.deleteRecursively();
}

// ---- 8. Recorder wiring, and the programmatic-apply guard ----

TEST_F(TimelineAppWiringTest, RecorderCapturesAGestureAndAProgrammaticLoadRecordsNothing) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& graphEditor = mc.getGraphEditor();
    graphEditor.newPatch();
    graphEditor.addModuleAtCanvasPosition("Filter", {200, 200}, {});
    mc.getUndoManager().clearUndoHistory();

    auto& graph = mc.getAudioEngine().getGraph();
    // graphToJSON assigns the lazily-generated uuids; the add-track flow does it for its own node,
    // but an ordinary module only gets one when the graph is next serialised.
    juce::ignoreUnused(synth::AIStateMapper::graphToJSON(graph));

    auto* filterNode = findNodeOfType(graph, ModuleType::Filter);
    ASSERT_NE(filterNode, nullptr);
    const juce::String filterUuid = filterNode->properties["uuid"].toString();
    ASSERT_TRUE(filterUuid.isNotEmpty());
    auto* cutoff = findParameterByID(filterNode->getProcessor(), "cutoff");
    ASSERT_NE(cutoff, nullptr);

    // A Touch lane on that parameter. Adding it is the ONLY wiring step: the doc notifies,
    // MainComponent republishes, and the recorder's bindings are rebuilt from the same resolution.
    auto& doc = mc.getTimelineDoc();
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    synth::AutomationLane::RangeSnapshot range;
    range.minValue = cutoff->getNormalisableRange().start;
    range.maxValue = cutoff->getNormalisableRange().end;
    range.defaultValue = cutoff->convertFrom0to1(cutoff->getDefaultValue());
    const auto laneId = doc.addLane(trackId, filterUuid, "cutoff", range);
    ASSERT_TRUE(laneId.isValid());
    ASSERT_TRUE(doc.setLaneRecordMode(laneId, (int)synth::LaneRecordMode::Touch));

    auto& recorder = mc.getAutomationRecorder();
    EXPECT_EQ(recorder.getNumBindings(), 1) << "publish-on-change rebound the recorder";

    auto& transport = mc.getAudioEngine().getTransport();
    recorder.setGlobalRecordEnable(true);
    ASSERT_TRUE(transport.play());
    transport.tick(512);
    ASSERT_TRUE(transport.getPositionSnapshot().playing);
    recorder.update();

    // The gesture. Ticking between the two writes puts the captured points on different beats.
    cutoff->beginChangeGesture();
    cutoff->setValueNotifyingHost(0.25f);
    transport.tick(512);
    cutoff->setValueNotifyingHost(0.75f);
    cutoff->endChangeGesture();
    recorder.update();

    const auto* lane = doc.getLane(laneId);
    ASSERT_NE(lane, nullptr);
    ASSERT_FALSE(lane->points.empty()) << "a Touch gesture on a bound parameter must be captured";
    const auto capturedPoints = lane->points.size();

    // A factory preset load pushes a whole patch's worth of parameter values. It must record
    // nothing — and it runs inside a ScopedProgrammaticApply, which this observes from the
    // graph-structure callback that fires while the load is still in progress.
    bool suspendedDuringLoad = false;
    graphEditor.onGraphStructureChanged = [&] { suspendedDuringLoad = recorder.isSuspended(); };
    mc.simulateLoadFactoryPresetForTest(0);
    EXPECT_TRUE(suspendedDuringLoad) << "the preset load must run inside a programmatic-apply scope";

    const auto* laneAfter = doc.getLane(laneId);
    ASSERT_NE(laneAfter, nullptr);
    EXPECT_EQ(laneAfter->points.size(), capturedPoints) << "nothing the preset wrote was captured";
    EXPECT_TRUE(laneAfter->orphaned) << "the lane's node is gone: orphaned and retained, never deleted";

    recorder.setGlobalRecordEnable(false);
    transport.stop();
    transport.tick(512);
}

#endif // SYNTH_ENABLE_TIMELINE

// paint() must not crash with a null transport (default-constructed ruler never had setTransport()
// called), at both a very zoomed-out and a very zoomed-in pixelsPerBeat.
TEST(TimelineRulerComponentTest, SnapshotSmokeAtTwoZoomsWithNullTransport) {
    synth::ui::TimelineViewState state;
    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setSize(800, 24);
    ASSERT_EQ(ruler.getTransport(), nullptr);

    state.pixelsPerBeat = synth::ui::TimelineViewState::kMinPixelsPerBeat;
    const juce::Image zoomedOut = ruler.createComponentSnapshot(ruler.getLocalBounds());
    EXPECT_FALSE(zoomedOut.isNull());
    EXPECT_EQ(zoomedOut.getWidth(), 800);
    EXPECT_EQ(zoomedOut.getHeight(), 24);

    state.pixelsPerBeat = synth::ui::TimelineViewState::kMaxPixelsPerBeat;
    const juce::Image zoomedIn = ruler.createComponentSnapshot(ruler.getLocalBounds());
    EXPECT_FALSE(zoomedIn.isNull());
    EXPECT_EQ(zoomedIn.getWidth(), 800);
    EXPECT_EQ(zoomedIn.getHeight(), 24);
}

// ============================================================================
// 5. Resizable panel height — the panel's top-edge grab strip (ungated) and MainComponent's
//    ownership of the value (gated, like the rest of group 2).
// ============================================================================

TEST(TimelinePanelComponentTest, AddTrackButtonCarriesATooltip) {
    synth::ui::TimelinePanelComponent panel;
    EXPECT_EQ(panel.getAddTrackButton().getTooltip(), "Add a MIDI or Audio track");
}

TEST(TimelinePanelResizeTest, GrabStripCoversTheTopEdgeAndKeepsTheTransportControlsClear) {
    using Panel = synth::ui::TimelinePanelComponent;
    Panel panel;
    panel.setSize(1200, 220);

    auto& handle = panel.getResizeHandle();
    EXPECT_EQ(handle.getBounds(), juce::Rectangle<int>(0, 0, 1200, Panel::kResizeHandleHeight));
    EXPECT_TRUE(handle.getMouseCursor() == juce::MouseCursor::UpDownResizeCursor);

    // The strip is chrome ON the transport strip, not a fourth region: transportBarBounds_ still
    // starts at y == 0 (PanelRegionsTile's tiling holds), but the controls inside start below it,
    // so a resize grab can never land on a transport button.
    EXPECT_EQ(panel.getTransportBarBounds().getY(), 0);
    EXPECT_GE(panel.getTransportBar().getY(), Panel::kResizeHandleHeight);
    EXPECT_GE(panel.getSnapCombo().getY(), Panel::kResizeHandleHeight);
}

TEST(TimelinePanelResizeTest, HoverStateFlipsOnlyOnEnterAndExit) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    auto& handle = panel.getResizeHandle();

    EXPECT_FALSE(panel.isResizeHandleHovered());
    handle.mouseEnter(makeClickEvent(handle, {10.0f, 2.0f}));
    EXPECT_TRUE(panel.isResizeHandleHovered());
    // A second enter is not a change — the strip repaints only when the state moves.
    handle.mouseEnter(makeClickEvent(handle, {40.0f, 3.0f}));
    EXPECT_TRUE(panel.isResizeHandleHovered());
    handle.mouseExit(makeClickEvent(handle, {40.0f, 3.0f}));
    EXPECT_FALSE(panel.isResizeHandleHovered());
}

// The panel reports a DESIRED height measured from its fixed bottom edge, unclamped — clamping and
// layout belong to the owner. Persistence is signalled once, on mouse-up.
TEST(TimelinePanelResizeTest, DraggingReportsTheHeightMeasuredFromTheFixedBottomEdge) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);

    std::vector<int> live, committed;
    panel.onResizeHeight = [&live](int h) { live.push_back(h); };
    panel.onResizeHeightCommitted = [&committed](int h) { committed.push_back(h); };

    auto& handle = panel.getResizeHandle();
    // Grabbed 2 px into the strip, dragged 62 px UP: bottom edge pinned, so 220 + 62.
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseDrag(makeDragEvent(handle, {10.0f, -60.0f}, {10.0f, 2.0f}));
    ASSERT_EQ(live.size(), 1u);
    EXPECT_EQ(live.front(), 282);
    EXPECT_TRUE(committed.empty()) << "nothing is committed mid-drag";

    handle.mouseUp(makeClickEvent(handle, {10.0f, -60.0f}));
    ASSERT_EQ(committed.size(), 1u);
    EXPECT_EQ(committed.front(), 282);
    EXPECT_EQ(live.size(), 1u) << "mouse-up adds no extra layout step";

    // Downward drag shrinks it, and the reported value is NOT clamped to the panel's minimum.
    live.clear();
    committed.clear();
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseDrag(makeDragEvent(handle, {10.0f, 90.0f}, {10.0f, 2.0f}));
    ASSERT_EQ(live.size(), 1u);
    EXPECT_EQ(live.front(), 132); // 220 - 88
    handle.mouseUp(makeClickEvent(handle, {10.0f, 90.0f}));

    // A drag that never began on the strip reports nothing.
    live.clear();
    committed.clear();
    handle.mouseDrag(makeDragEvent(handle, {10.0f, -300.0f}, {10.0f, 2.0f}));
    handle.mouseUp(makeClickEvent(handle, {10.0f, -300.0f}));
    EXPECT_TRUE(live.empty());
    EXPECT_TRUE(committed.empty());

    // A click that never dragged commits nothing either — no settings write on a stray click.
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    handle.mouseUp(makeClickEvent(handle, {10.0f, 2.0f}));
    EXPECT_TRUE(live.empty());
    EXPECT_TRUE(committed.empty());
}

// Nothing inside the panel assumes the default height: at 2x, every extra pixel goes to the lanes.
TEST(TimelinePanelResizeTest, InternalLayoutHoldsAtDoubleHeight) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);
    const int lanesAtDefault = panel.getLanesBounds().getHeight();

    panel.setSize(1200, 440);
    const auto transport = panel.getTransportBarBounds();
    const auto trackHeader = panel.getTrackHeaderBounds();
    const auto lanes = panel.getLanesBounds();
    EXPECT_EQ(transport.getUnion(trackHeader).getUnion(lanes), panel.getLocalBounds());
    EXPECT_EQ(transport.getHeight(), 28) << "the transport strip keeps its metric height";
    EXPECT_EQ(lanes.getHeight(), lanesAtDefault + 220) << "the extra height all goes to the lanes";
    EXPECT_EQ(panel.getResizeHandle().getWidth(), 1200);

    // The clip lanes still fill the lanes region below the 24 px ruler — the rect the grid is
    // painted into, so clips stay aligned with it at any height.
    EXPECT_EQ(panel.getClipLaneArea().getBounds(), lanes.withTrimmedTop(24));
    EXPECT_EQ(panel.getPianoRoll().getBounds(), lanes.withTrimmedTop(24));
    // The header list fills the taller viewport even with no tracks (no gap under the last row).
    ASSERT_NE(panel.getTrackHeaderViewport().getViewedComponent(), nullptr);
    EXPECT_GE(panel.getTrackHeaderViewport().getViewedComponent()->getHeight(),
              panel.getTrackHeaderViewport().getMaximumVisibleHeight());

    const juce::Image img = panel.createComponentSnapshot(panel.getLocalBounds());
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.getHeight(), 440);
}

#if SYNTH_ENABLE_TIMELINE

namespace {
// Leaves a height in the shared settings file the way an earlier session would have — the same file
// TimelinePanelIntegrationTest::resetPanelKeys() clears the key from.
void persistTimelinePanelHeight(int height) {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::userSettingsOptions());
    if (auto* s = props.getUserSettings()) {
        s->setValue(MainComponent::kTimelinePanelHeightKey, height);
        s->saveIfNeeded();
    }
}

int readPersistedTimelinePanelHeight(MainComponent& mc) {
    return mc.getAppPropertiesForTest().getUserSettings()->getIntValue(MainComponent::kTimelinePanelHeightKey, -1);
}
} // namespace

TEST_F(TimelinePanelIntegrationTest, AbsentSettingFallsBackToTheThemeMetric) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();

    EXPECT_EQ(mc.getTimelinePanelHeight(), 220); // Metrics::timelinePanelHeight literal default
    EXPECT_EQ(mc.getTimelinePanel().getBounds().getHeight(), 220);
    EXPECT_EQ(readPersistedTimelinePanelHeight(mc), -1) << "showing the panel writes no height";
}

TEST_F(TimelinePanelIntegrationTest, PersistedHeightIsHonouredAtStartup) {
    persistTimelinePanelHeight(400);

    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.getTimelinePanel().isVisible());

    const auto panelBounds = mc.getTimelinePanel().getBounds();
    EXPECT_EQ(mc.getTimelinePanelHeight(), 400);
    EXPECT_EQ(panelBounds.getHeight(), 400);
    EXPECT_EQ(panelBounds.getBottom(), mc.getStatusBar().getBounds().getY());
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), panelBounds.getY());
}

TEST_F(TimelinePanelIntegrationTest, DraggingTheGrabStripResizesLiveAndPersistsOnDragEnd) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();

    auto& panel = mc.getTimelinePanel();
    ASSERT_EQ(panel.getBounds().getHeight(), 220);

    auto& handle = panel.getResizeHandle();
    handle.mouseDown(makeClickEvent(handle, {10.0f, 2.0f}));
    // 142 px above the grab point, against a pinned bottom edge: 220 + 142.
    handle.mouseDrag(makeDragEvent(handle, {10.0f, -140.0f}, {10.0f, 2.0f}));

    // LIVE: the owner already re-laid out, before any mouse-up.
    EXPECT_EQ(mc.getTimelinePanelHeight(), 362);
    EXPECT_EQ(panel.getBounds().getHeight(), 362);
    EXPECT_EQ(panel.getBounds().getBottom(), mc.getStatusBar().getBounds().getY());
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), panel.getBounds().getY());
    EXPECT_EQ(readPersistedTimelinePanelHeight(mc), -1) << "not persisted per pixel";

    handle.mouseUp(makeClickEvent(handle, {10.0f, -140.0f}));
    EXPECT_EQ(readPersistedTimelinePanelHeight(mc), 362);

    // A second component reads the same file back — and shows the panel at that height.
    MainComponent mc2(std::make_unique<MockProviderTL>());
    mc2.setSize(1600, 900);
    EXPECT_EQ(mc2.getTimelinePanelHeight(), 362);
}

TEST_F(TimelinePanelIntegrationTest, HeightIsClampedToTheMetricFloorAndThreeQuartersOfTheWindow) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();

    auto& panel = mc.getTimelinePanel();
    ASSERT_TRUE(panel.onResizeHeight != nullptr);

    panel.onResizeHeight(5000);
    EXPECT_EQ(mc.getTimelinePanelHeight(), 675) << "75% of the 900 px window";
    EXPECT_EQ(panel.getBounds().getHeight(), 675);
    EXPECT_GT(mc.getGraphEditor().getBounds().getHeight(), 0);

    panel.onResizeHeight(10);
    EXPECT_EQ(mc.getTimelinePanelHeight(), 220) << "the theme metric is the floor";
    EXPECT_EQ(panel.getBounds().getHeight(), 220);

    EXPECT_EQ(readPersistedTimelinePanelHeight(mc), -1) << "only the drag-end callback persists";
}

TEST_F(TimelinePanelIntegrationTest, ASmallerWindowReclampsTheHeightSoTheCanvasSurvives) {
    persistTimelinePanelHeight(600);

    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();
    ASSERT_EQ(mc.getTimelinePanelHeight(), 600); // within 75% of 900

    mc.setSize(1000, 400);
    EXPECT_EQ(mc.getTimelinePanelHeight(), 300) << "75% of the 400 px window";
    EXPECT_EQ(mc.getTimelinePanel().getBounds().getHeight(), 300);
    EXPECT_GT(mc.getGraphEditor().getBounds().getHeight(), 0);
    EXPECT_EQ(mc.getTimelinePanel().getBounds().getBottom(), mc.getStatusBar().getBounds().getY());

    // Shorter than 4/3 of the floor (below the enforced minWindowHeight, so a corner case only):
    // the floor wins rather than the cap.
    mc.setSize(1000, 280);
    EXPECT_EQ(mc.getTimelinePanelHeight(), 220);
}

TEST_F(TimelinePanelIntegrationTest, HidingThePanelReturnsTheCanvasAndReshowingKeepsTheDraggedHeight) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    const auto canvasWithNoPanel = mc.getGraphEditor().getBounds();

    mc.simulateToggleTimelineClick();
    mc.getTimelinePanel().onResizeHeight(420);
    ASSERT_EQ(mc.getTimelinePanel().getBounds().getHeight(), 420);

    mc.simulateToggleTimelineClick(); // hide
    EXPECT_FALSE(mc.getTimelinePanel().isVisible());
    EXPECT_EQ(mc.getGraphEditor().getBounds(), canvasWithNoPanel) << "a hidden panel carves nothing, at any height";
    EXPECT_EQ(mc.getTimelinePanelHeight(), 420) << "the height outlives a hide";

    mc.simulateToggleTimelineClick(); // show again
    EXPECT_EQ(mc.getTimelinePanel().getBounds().getHeight(), 420);
    EXPECT_EQ(mc.getTimelinePanel().getBounds().getBottom(), mc.getStatusBar().getBounds().getY());
}

#endif // SYNTH_ENABLE_TIMELINE

// ============================================================================
// 6. Authoring gestures reaching the panel (ungated, like groups 1/3/4 — the panel itself always
//    compiles). The lane-area half of the gesture (snapping, length, one undo step) lives in
//    Tests/TimelineClipLaneTests.cpp group 7; the import half in Tests/AssetManagerTests.cpp.
// ============================================================================

// The panel's onClipDoubleClicked -> openPianoRoll wiring has to cover the clip a double-click on
// EMPTY MIDI lane space just created, not only an existing one being reopened.
TEST(TimelinePanelComponentTest, DoubleClickOnEmptyMidiLaneCreatesAClipAndOpensThePianoRoll) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 320);
    panel.getViewState().pixelsPerBeat = 40.0;
    panel.getViewState().firstVisibleBeat = 0.0;
    panel.getViewState().snap = synth::ui::TimelineViewState::Snap::Bar;

    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    ASSERT_TRUE(trackId.isValid());
    ASSERT_FALSE(panel.isPianoRollOpen());

    auto& lane = panel.getClipLaneArea();
    lane.mouseDoubleClick(makeClickEvent(lane, {200.0f, (float)(lane.getRowHeight() / 2)}));

    ASSERT_NE(doc.getTrack(trackId), nullptr);
    ASSERT_EQ(doc.getTrack(trackId)->clips.size(), 1u);
    const auto& clip = doc.getTrack(trackId)->clips[0];
    EXPECT_DOUBLE_EQ(clip.startBeat, 4.0) << "x = 200 px -> beat 5.0, floored onto the bar grid";
    EXPECT_DOUBLE_EQ(clip.lengthBeats, 4.0);
    ASSERT_TRUE(panel.isPianoRollOpen()) << "the user lands in the note editor, ready to draw";
    EXPECT_EQ(panel.getPianoRoll().getClipId(), clip.id);
    EXPECT_FALSE(lane.isVisible()) << "the piano roll replaced the lanes, same as reopening a clip";
}
