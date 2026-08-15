// TimelinePanelTests.cpp
//
// TL5-1: bottom-docked timeline panel shell + toolbar toggle + rebindable shortcut + slide
// animation.
//
// Two groups of coverage:
//   1. synth::ui::TimelinePanelComponent in isolation — pure layout/paint, no MainComponent, no
//      SYNTH_ENABLE_TIMELINE gating (the component itself always compiles; only MainComponent's
//      use of it is gated).
//   2. MainComponent integration — toggle, persistence, carve geometry. Gated
//      #if SYNTH_ENABLE_TIMELINE because the toggle button/command/carve compile out entirely
//      when the flag is OFF (see MainComponent.h/.cpp). HiddenByDefaultAndCarvesNothing is the
//      exception: it asserts the flag-OFF invariant too (nothing timeline-related visible or
//      carved), so it is deliberately NOT gated.
//
// TL5-2 adds a third group at the bottom of this file:
//   3. Ruler/grid/zoom/scroll/snap/loop-brace — synth::ui::TimelineRulerComponent and the panel's
//      wheel handling and snap combo, driven with a raw synth::TransportService (never through
//      MainComponent/AudioEngine), so — same reasoning as group 1 — none of it is gated either.

#include "../Source/AI/AIProvider.h"
#include "../Source/Transport/TransportService.h"
#include "../Source/UI/TimelinePanelComponent.h"
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
// 3. TL5-2: ruler/grid/zoom/scroll/snap/loop-brace.
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

} // namespace

// A plain click (mouseDown then mouseUp at the same position, no drag) seeks the transport to the
// snapped beat under the click.
TEST(TimelineRulerInteractionTest, ClickSeeksSnapped) {
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
    const juce::Point<float> pos(180.0f, 12.0f);
    ruler.mouseDown(makeClickEvent(ruler, pos));
    ruler.mouseUp(makeClickEvent(ruler, pos));

    transport.tick(512); // drains the posted locateBeat() command

    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 5.0);
}

// Cmd+click (no drag) toggles looping off, keeping the prior bounds.
TEST(TimelineRulerInteractionTest, CommandClickTogglesLoopOffKeepingBounds) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    ASSERT_TRUE(transport.setLoop(2.0, 6.0, true));
    transport.tick(512);
    ASSERT_TRUE(transport.getPositionSnapshot().looping);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);

    const juce::Point<float> pos(100.0f, 12.0f);
    ruler.mouseDown(makeClickEvent(ruler, pos, juce::ModifierKeys(juce::ModifierKeys::commandModifier)));
    ruler.mouseUp(makeClickEvent(ruler, pos, juce::ModifierKeys(juce::ModifierKeys::commandModifier)));
    transport.tick(512);

    const auto snap = transport.getPositionSnapshot();
    EXPECT_FALSE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);
}

// Press-drag-release sets the loop to the snapped [min,max] range; dragging leftwards (releasing
// before the press point) must still normalise start < end.
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
    const juce::Point<float> pressPos(170.0f, 12.0f);
    const juce::Point<float> releasePos(50.0f, 12.0f);

    ruler.mouseDown(makeClickEvent(ruler, pressPos));
    ruler.mouseDrag(makeDragEvent(ruler, releasePos, pressPos));
    ruler.mouseUp(makeDragEvent(ruler, releasePos, pressPos));

    transport.tick(512);

    const auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 4.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 8.0);
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
