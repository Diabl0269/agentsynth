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
#include "../Source/AppUndoManager.h"
#include "../Source/ProjectBundle.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Transport/TransportService.h"
#include "../Source/UI/EdgeAutoScroll.h"
#include "../Source/UI/TimelinePanelComponent.h"
#include "../Source/UI/TrackColour.h"
#include "../Source/UserSettings.h"
#include "MainComponent.h"
#include <algorithm>
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
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
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
            s->setValue("librarySidebarVisible", "1");  // default: visible
            s->setValue("aiPanelVisible", "0");         // default: hidden
            s->setValue("minimapVisible", "1");         // default: visible
            s->setValue("timelinePanelVisible", "0");   // default: hidden
            s->setValue("timelineFeatureEnabled", "1"); // default: enabled (the kill switch is off)
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

// ----------------------------------------------------------------------------
// Preferences "Show timeline (experimental)" kill switch — hides the user-facing entry points
// (toolbar button, Cmd+T, Space) without touching the timeline doc or audio-engine playback.
// See MainComponent::applyTimelineFeatureEnabled().
// ----------------------------------------------------------------------------

TEST_F(TimelinePanelIntegrationTest, DisablingFeatureHidesVisiblePanelAndButton) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());
    ASSERT_TRUE(mc.getTimelinePanel().isVisible());

    mc.applyTimelineFeatureEnabled(false);
    EXPECT_FALSE(mc.isTimelineFeatureEnabledForTest());
    // Hidden via the SAME path as a manual toolbar click — persisted, not just visually hidden.
    EXPECT_FALSE(mc.isTimelineConfiguredVisible());
    EXPECT_FALSE(mc.getTimelinePanel().isVisible());
    EXPECT_FALSE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("timelinePanelVisible", true));
}

TEST_F(TimelinePanelIntegrationTest, ReenablingFeatureRestoresButtonNotPanel) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);

    mc.applyTimelineFeatureEnabled(false);
    ASSERT_FALSE(mc.isTimelineFeatureEnabledForTest());

    mc.applyTimelineFeatureEnabled(true);
    EXPECT_TRUE(mc.isTimelineFeatureEnabledForTest());
    // Re-enabling brings the button back but does not itself reopen the panel — that stays a
    // separate, explicit user action (toolbar click / Cmd+T).
    EXPECT_FALSE(mc.isTimelineConfiguredVisible());
}

TEST_F(TimelinePanelIntegrationTest, CmdTIsInertWhileFeatureDisabled) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    mc.applyTimelineFeatureEnabled(false);

    auto& cm = mc.getCommandManager();
    juce::ApplicationCommandInfo info(AppCommands::toggleTimelinePanel);
    mc.getCommandInfo(AppCommands::toggleTimelinePanel, info);
    EXPECT_NE(info.flags & juce::ApplicationCommandInfo::isDisabled, 0)
        << "toggleTimelinePanel must be inactive while the timeline feature preference is off";
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::toggleTimelinePanel, false));
    EXPECT_FALSE(mc.isTimelineConfiguredVisible());

    juce::ApplicationCommandInfo playbackInfo(AppCommands::togglePlayback);
    mc.getCommandInfo(AppCommands::togglePlayback, playbackInfo);
    EXPECT_NE(playbackInfo.flags & juce::ApplicationCommandInfo::isDisabled, 0)
        << "togglePlayback (Space) must be inactive while the timeline feature preference is off";
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
    state.snap = synth::ui::TimelineViewState::Snap::Quarter;

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
    state.snap = synth::ui::TimelineViewState::Snap::Quarter;

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
    state.snap = synth::ui::TimelineViewState::Snap::Quarter;

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
    state.snap = synth::ui::TimelineViewState::Snap::Quarter;

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

// The locators stay VISIBLE when looping is off: a range that exists is always braced, just greyed
// out. Three states — range+armed, range+disarmed, no range at all.
TEST(TimelineRulerInteractionTest, LoopBraceStaysVisibleWhileDisarmed) {
    using Ruler = synth::ui::TimelineRulerComponent;
    using BraceState = Ruler::BraceState;

    // The pure rule first: only the range's existence decides whether a brace is drawn at all.
    EXPECT_EQ(Ruler::braceStateFor(true, 2.0, 6.0), BraceState::Active);
    EXPECT_EQ(Ruler::braceStateFor(false, 2.0, 6.0), BraceState::Inactive);
    EXPECT_EQ(Ruler::braceStateFor(false, 2.0, 2.0), BraceState::None);
    EXPECT_EQ(Ruler::braceStateFor(true, 6.0, 2.0), BraceState::None) << "a reversed range is not a range";

    // ...and that a disarmed brace is drawn in a visibly DIFFERENT colour from an armed one (the
    // muted text token, not a faded accent — the hover band is already accent at 10%).
    const juce::Colour accent(0xff00D1FF), textMuted(0xff8A93A0);
    EXPECT_EQ(Ruler::braceColourFor(BraceState::Active, accent, textMuted), accent);
    EXPECT_NE(Ruler::braceColourFor(BraceState::Inactive, accent, textMuted), accent);

    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    Ruler ruler(state);
    ruler.setSize(800, 24);
    EXPECT_EQ(ruler.getBraceStateForTest(), BraceState::None) << "no transport: nothing to brace";

    ruler.setTransport(&transport);
    ASSERT_TRUE(transport.setLoop(2.0, 6.0, true));
    transport.tick(512);
    EXPECT_EQ(ruler.getBraceStateForTest(), BraceState::Active);

    // Cmd+click disarms it — the brace must survive, dimmed, rather than vanishing.
    const juce::ModifierKeys cmd(juce::ModifierKeys::commandModifier);
    ruler.mouseDown(makeClickEvent(ruler, {100.0f, kLoopZoneY}, cmd));
    transport.tick(512);
    ASSERT_FALSE(transport.getPositionSnapshot().looping);
    EXPECT_EQ(ruler.getBraceStateForTest(), BraceState::Inactive);

    // A collapsed range has no locators to show at all.
    ASSERT_TRUE(transport.setLoop(3.0, 3.0, true));
    transport.tick(512);
    EXPECT_EQ(ruler.getBraceStateForTest(), BraceState::None);

    EXPECT_FALSE(ruler.createComponentSnapshot(ruler.getLocalBounds()).isNull());
}

// A plain click on the dimmed brace re-arms the existing range (the inverse of the Cmd+click that
// disarmed it). Outside the brace's span, a no-drag loop-zone click stays inert.
TEST(TimelineRulerInteractionTest, ClickOnDimmedBraceReArmsLooping) {
    synth::ui::TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.firstVisibleBeat = 0.0;
    state.snap = synth::ui::TimelineViewState::Snap::Quarter;

    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    ASSERT_TRUE(transport.setLoop(2.0, 6.0, false)); // a range exists, looping off -> dimmed brace
    transport.tick(512);

    synth::ui::TimelineRulerComponent ruler(state);
    ruler.setTransport(&transport);
    ruler.setSize(800, 24);
    ASSERT_EQ(ruler.getBraceStateForTest(), synth::ui::TimelineRulerComponent::BraceState::Inactive);

    // x = 400px -> beat 10: past the brace's [80, 240] px span, so nothing happens.
    const juce::Point<float> outside(400.0f, kLoopZoneY);
    ruler.mouseDown(makeClickEvent(ruler, outside));
    ruler.mouseUp(makeClickEvent(ruler, outside));
    transport.tick(512);
    auto snap = transport.getPositionSnapshot();
    EXPECT_FALSE(snap.looping) << "a click off the brace must not arm anything";
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);

    // x = 160px -> beat 4, inside [2, 6): re-arms, bounds untouched.
    const juce::Point<float> onBrace(160.0f, kLoopZoneY);
    ruler.mouseDown(makeClickEvent(ruler, onBrace));
    ruler.mouseUp(makeClickEvent(ruler, onBrace));
    transport.tick(512);
    snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);
    EXPECT_EQ(ruler.getSeekPostCountForTest(), 0) << "a loop-zone click never moves the playhead";

    // Clicking an ALREADY-armed brace stays inert (it is not a toggle — Cmd+click is).
    ruler.mouseDown(makeClickEvent(ruler, onBrace));
    ruler.mouseUp(makeClickEvent(ruler, onBrace));
    transport.tick(512);
    EXPECT_TRUE(transport.getPositionSnapshot().looping);
}

// Wheel bindings (Cubase-style): plain vertical wheel scrolls the TRACK rows, Shift+wheel (or a
// trackpad's own deltaX) scrolls horizontally, Cmd+wheel zooms horizontally around the cursor
// (keeping the beat under it fixed), Cmd+Shift+wheel zooms the row height.
TEST(TimelinePanelInteractionTest, WheelScrollsAndCmdWheelZooms) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 220);
    // Enough tracks that the rows overflow the visible lanes height and vertical scroll has range.
    for (int i = 0; i < 24; ++i)
        doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(i));

    auto& state = panel.getViewState();
    // Start comfortably away from the firstVisibleBeat >= 0 clamp so the scroll below is visible
    // regardless of which wheel direction maps to which scroll direction.
    state.firstVisibleBeat = 500.0;
    const double ppbBefore = state.pixelsPerBeat;
    const double firstVisibleBefore = state.firstVisibleBeat;

    // Plain vertical wheel: vertical track scroll, horizontal mapping untouched.
    juce::MouseWheelDetails wheel{}; // value-init: deltaX has no default and the router reads it
    wheel.deltaY = -0.5f;            // wheel down -> scroll down into the track list
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}), wheel);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, ppbBefore);
    EXPECT_DOUBLE_EQ(state.firstVisibleBeat, firstVisibleBefore);
    EXPECT_GT(state.trackScrollY, 0.0);
    EXPECT_EQ(panel.getTrackHeaderViewport().getViewPositionY(), (int)std::llround(state.trackScrollY))
        << "the header column follows the shared vertical scroll";

    // Shift+wheel: horizontal scroll, vertical untouched.
    const double trackScrollBefore = state.trackScrollY;
    juce::MouseWheelDetails hWheel{};
    hWheel.deltaY = 0.5f;
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}, juce::ModifierKeys(juce::ModifierKeys::shiftModifier)),
                         hWheel);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, ppbBefore);
    EXPECT_NE(state.firstVisibleBeat, firstVisibleBefore);
    EXPECT_DOUBLE_EQ(state.trackScrollY, trackScrollBefore);

    // Cmd+Shift+wheel: vertical (row height) zoom within its clamps.
    const double scaleBefore = state.rowHeightScale;
    juce::MouseWheelDetails vZoomWheel{};
    vZoomWheel.deltaY = 0.5f;
    panel.mouseWheelMove(
        makeClickEvent(panel, {400.0f, 100.0f},
                       juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier)),
        vZoomWheel);
    EXPECT_GT(state.rowHeightScale, scaleBefore);
    EXPECT_LE(state.rowHeightScale, synth::ui::TimelineViewState::kMaxRowHeightScale);

    const juce::Point<float> cursor(300.0f, 12.0f);
    // The ruler shares TimelineViewState's x==0 origin, so reproject into its coordinate space to
    // compute the same anchor beat the panel's mouseWheelMove uses internally.
    const double anchorXInRuler = (double)cursor.x - (double)panel.getRuler().getX();
    const double anchorBeatBefore = state.xToBeat(anchorXInRuler);
    const double ppbBeforeZoom = state.pixelsPerBeat;

    juce::MouseWheelDetails zoomWheel{};
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
    ASSERT_EQ(panel.getViewState().snap, synth::ui::TimelineViewState::Snap::Quarter); // documented default

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

// ---- setSnapValue / cycleSnapValue: the view-state verbs behind the snap shortcuts ----

// setSnapValue is the ONE writer the combo, the shortcut layer and cycleSnapValue share: it moves
// the view state, mirrors the combo, re-arms the master switch (same meaning a combo pick has) and
// persists through the existing persistSnapChoice() path.
TEST(TimelinePanelSnapApiTest, SetSnapValueSyncsTheComboAndPersistsThroughTheExistingPath) {
    using Snap = synth::ui::TimelineViewState::Snap;

    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth Timeline Snap Api Test";
    opts.folderName = "Agent Synth Timeline Snap Api Test";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;

    // Hermetic regardless of a previous run — same idiom as SnapChoicePersists above.
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
    ASSERT_EQ(panel.getViewState().snap, Snap::Quarter); // documented default, freshly-deleted file

    EXPECT_TRUE(panel.setSnapValue(Snap::Eighth));
    EXPECT_EQ(panel.getViewState().snap, Snap::Eighth);
    EXPECT_EQ(panel.getSnapCombo().getSelectedId(), (int)Snap::Eighth + 1) << "the combo mirrors the view state";
    EXPECT_TRUE(panel.getViewState().snapEnabled);
    ASSERT_NE(props.getUserSettings(), nullptr);
    EXPECT_EQ(props.getUserSettings()->getIntValue("timelineSnap", -1), (int)Snap::Eighth);

    // Turn magnetism off (the Q button — the shared switch), then re-pick the SAME division: no
    // change is reported, but it still re-arms, exactly like re-picking from the combo.
    panel.getSnapToggleButton().onClick();
    ASSERT_FALSE(panel.getViewState().snapEnabled);
    EXPECT_FALSE(panel.setSnapValue(Snap::Eighth));
    EXPECT_TRUE(panel.getViewState().snapEnabled);

    // Reset the persisted keys so the next run (and every other test) starts from nothing.
    if (auto* s = props.getUserSettings())
        s->getFile().deleteFile();
}

// The cycle walks Bar -> 1 -> 1/2 -> 1/4 -> 1/8 -> 1/16 -> 1/32 -> 1/64 -> 1/128 and CLAMPS at both
// ends: it never wraps and never lands on Off (that is the Q key's job). The finest stop moved from
// Sixteenth to HundredTwentyEighth when the three finer divisions were added — see
// TimelineViewState::Snap's class comment for why they were appended after Sixteenth rather than
// inserted in note-value order.
TEST(TimelinePanelSnapApiTest, CycleWalksTheMusicalDivisionsAndClampsAtBothEnds) {
    using Snap = synth::ui::TimelineViewState::Snap;
    synth::ui::TimelinePanelComponent panel; // no ApplicationProperties: nothing reads or writes settings
    panel.getViewState().setSnap(Snap::Quarter);
    panel.getViewState().snapEnabled = true;

    // Coarser, down to the Bar end, then one more press that must change nothing.
    EXPECT_TRUE(panel.cycleSnapValue(-1));
    EXPECT_EQ(panel.getViewState().snap, Snap::Half);
    EXPECT_TRUE(panel.cycleSnapValue(-1));
    EXPECT_EQ(panel.getViewState().snap, Snap::Whole);
    EXPECT_TRUE(panel.cycleSnapValue(-1));
    EXPECT_EQ(panel.getViewState().snap, Snap::Bar);
    EXPECT_FALSE(panel.cycleSnapValue(-1)) << "clamped at Bar — no wrap, and never Off";
    EXPECT_EQ(panel.getViewState().snap, Snap::Bar);

    // Finer, all the way up the list, now through the three new fine divisions.
    for (auto expected : {Snap::Whole, Snap::Half, Snap::Quarter, Snap::Eighth, Snap::Sixteenth, Snap::ThirtySecond,
                          Snap::SixtyFourth, Snap::HundredTwentyEighth}) {
        EXPECT_TRUE(panel.cycleSnapValue(1));
        EXPECT_EQ(panel.getViewState().snap, expected);
    }
    EXPECT_FALSE(panel.cycleSnapValue(1)) << "clamped at 1/128 — a held key parks here";
    EXPECT_EQ(panel.getViewState().snap, Snap::HundredTwentyEighth);
    EXPECT_EQ(panel.getSnapCombo().getSelectedId(), (int)Snap::HundredTwentyEighth + 1);

    EXPECT_FALSE(panel.cycleSnapValue(0)) << "no direction, no move";
    EXPECT_EQ(panel.getViewState().snap, Snap::HundredTwentyEighth);
}

// From Off, BOTH directions re-enter at the last division the user actually chose — with the view
// state's default as the fallback when nothing was ever chosen.
TEST(TimelinePanelSnapApiTest, CyclingFromOffReEntersAtTheLastMusicalDivision) {
    using Snap = synth::ui::TimelineViewState::Snap;
    synth::ui::TimelinePanelComponent panel;
    panel.getViewState().snapEnabled = true;

    ASSERT_TRUE(panel.setSnapValue(Snap::Eighth)); // the "last musical" value from here on
    ASSERT_TRUE(panel.setSnapValue(Snap::Off));
    EXPECT_TRUE(panel.cycleSnapValue(1));
    EXPECT_EQ(panel.getViewState().snap, Snap::Eighth) << "finer-from-Off resumes where the user was";

    ASSERT_TRUE(panel.setSnapValue(Snap::Off));
    EXPECT_TRUE(panel.cycleSnapValue(-1));
    EXPECT_EQ(panel.getViewState().snap, Snap::Eighth) << "coarser-from-Off follows the same one rule";

    // A panel that never had a musical division picked falls back to the view state's default.
    synth::ui::TimelinePanelComponent fresh;
    fresh.getViewState().snap = Snap::Off; // straight assignment: never went through setSnap()
    EXPECT_TRUE(fresh.cycleSnapValue(1));
    EXPECT_EQ(fresh.getViewState().snap, Snap::Quarter);
}

// ---- The three finest divisions (1/32, 1/64, 1/128) added alongside Sixteenth ----

// A note value is a fraction of a whole note (4 beats — see TimelineViewState::divisionBeatsRaw's
// header comment), so 1/32 is an eighth of a beat, 1/64 a sixteenth, 1/128 a thirty-second — each
// half the one before it, same as Eighth->Sixteenth already was.
TEST(TimelinePanelSnapApiTest, DivisionBeatsForTheThreeNewFineSnaps) {
    using Snap = synth::ui::TimelineViewState::Snap;
    synth::ui::TimelinePanelComponent panel;
    auto& state = panel.getViewState();

    state.snap = Snap::ThirtySecond;
    EXPECT_DOUBLE_EQ(state.divisionBeats(4.0), 0.125);
    EXPECT_DOUBLE_EQ(state.divisionBeatsRaw(4.0), 0.125);

    state.snap = Snap::SixtyFourth;
    EXPECT_DOUBLE_EQ(state.divisionBeats(4.0), 0.0625);
    EXPECT_DOUBLE_EQ(state.divisionBeatsRaw(4.0), 0.0625);

    state.snap = Snap::HundredTwentyEighth;
    EXPECT_DOUBLE_EQ(state.divisionBeats(4.0), 0.03125);
    EXPECT_DOUBLE_EQ(state.divisionBeatsRaw(4.0), 0.03125);

    // beatsPerBar is irrelevant below Snap::Bar — same contract every other musical division has.
    EXPECT_DOUBLE_EQ(state.divisionBeats(3.0), 0.03125);
}

// The combo's ids follow the (int)snap + 1 convention all the way through the new entries, and
// picking one from the combo reaches the view state through the same setSnapValue path as every
// other division.
TEST(TimelinePanelSnapComboTest, ComboShowsAndSetsTheThreeNewFineDivisions) {
    using Snap = synth::ui::TimelineViewState::Snap;
    synth::ui::TimelinePanelComponent panel;
    auto& combo = panel.getSnapCombo();

    EXPECT_EQ(combo.getItemText(combo.indexOfItemId((int)Snap::ThirtySecond + 1)), "1/32");
    EXPECT_EQ(combo.getItemText(combo.indexOfItemId((int)Snap::SixtyFourth + 1)), "1/64");
    EXPECT_EQ(combo.getItemText(combo.indexOfItemId((int)Snap::HundredTwentyEighth + 1)), "1/128");

    combo.setSelectedId((int)Snap::SixtyFourth + 1, juce::sendNotificationSync);
    EXPECT_EQ(panel.getViewState().snap, Snap::SixtyFourth);

    EXPECT_TRUE(panel.setSnapValue(Snap::HundredTwentyEighth));
    EXPECT_EQ(combo.getSelectedId(), (int)Snap::HundredTwentyEighth + 1) << "the combo mirrors the view state";
}

// Same persistence path as SnapChoicePersists above, exercised at one of the new fine divisions —
// the jlimit clamp in setApplicationProperties() had to move to HundredTwentyEighth alongside the
// combo/cycle bounds, and this is what proves a restored fine value survives it rather than being
// silently clamped back down to Sixteenth.
TEST(TimelinePanelSnapComboTest, AFineSnapPersistsAndRestoresThroughTheExistingPath) {
    using Snap = synth::ui::TimelineViewState::Snap;

    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth Timeline Fine Snap Test";
    opts.folderName = "Agent Synth Timeline Fine Snap Test";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;

    // Hermetic regardless of a previous run — same idiom as SnapChoicePersists above.
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
    ASSERT_TRUE(panel.setSnapValue(Snap::HundredTwentyEighth));

    synth::ui::TimelinePanelComponent panel2;
    panel2.setApplicationProperties(&props);
    EXPECT_EQ(panel2.getViewState().snap, Snap::HundredTwentyEighth);
    EXPECT_EQ(panel2.getSnapCombo().getSelectedId(), (int)Snap::HundredTwentyEighth + 1);

    // Reset the persisted keys, per this file's pattern, so no other test inherits them.
    if (auto* s = props.getUserSettings())
        s->getFile().deleteFile();
}

// ---- zoomTimelineHorizontal / zoomTimelineVertical: the same paths the wheel/pinch use ----

TEST(TimelinePanelZoomApiTest, HorizontalZoomKeepsTheVisibleCentreBeatAndClamps) {
    using View = synth::ui::TimelineViewState;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 220);

    auto& state = panel.getViewState();
    state.pixelsPerBeat = 24.0;
    state.firstVisibleBeat = 100.0;

    // The panel anchors a keyboard zoom on the middle of the ruler strip, whose local x == 0 is the
    // view state's own origin.
    const double centreX = (double)panel.getRuler().getWidth() * 0.5;
    const double centreBeat = state.xToBeat(centreX);

    panel.zoomTimelineHorizontal(2.0);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, 48.0);
    EXPECT_NEAR(state.xToBeat(centreX), centreBeat, 1e-6) << "the centre beat stays under the centre pixel";

    for (int i = 0; i < 20; ++i)
        panel.zoomTimelineHorizontal(2.0);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, View::kMaxPixelsPerBeat);

    for (int i = 0; i < 40; ++i)
        panel.zoomTimelineHorizontal(0.5);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, View::kMinPixelsPerBeat);

    // Garbage factors are ignored rather than propagated into the mapping.
    const double settled = state.pixelsPerBeat;
    panel.zoomTimelineHorizontal(0.0);
    panel.zoomTimelineHorizontal(-2.0);
    EXPECT_DOUBLE_EQ(state.pixelsPerBeat, settled);
}

TEST(TimelinePanelZoomApiTest, VerticalZoomScalesTheRowHeightWithinItsClampsAndRelaysTheHeaders) {
    using View = synth::ui::TimelineViewState;
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 220);
    for (int i = 0; i < 24; ++i)
        doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(i));

    auto& state = panel.getViewState();
    state.rowHeightScale = 1.0;
    state.trackScrollY = 0.0;
    ASSERT_NE(panel.getTrackHeaderAt(0), nullptr);
    const int rowHeightBefore = panel.getTrackHeaderAt(0)->getHeight();

    panel.zoomTimelineVertical(1.5);
    EXPECT_GT(state.rowHeightScale, 1.0);
    EXPECT_GT(panel.getTrackHeaderAt(0)->getHeight(), rowHeightBefore)
        << "the header column is relaid out by the same path the wheel zoom uses";

    for (int i = 0; i < 10; ++i)
        panel.zoomTimelineVertical(2.0);
    EXPECT_DOUBLE_EQ(state.rowHeightScale, View::kMaxRowHeightScale);

    for (int i = 0; i < 20; ++i)
        panel.zoomTimelineVertical(0.5);
    EXPECT_DOUBLE_EQ(state.rowHeightScale, View::kMinRowHeightScale);

    const double settled = state.rowHeightScale;
    panel.zoomTimelineVertical(0.0);
    panel.zoomTimelineVertical(-2.0);
    EXPECT_DOUBLE_EQ(state.rowHeightScale, settled);
}

// ---- Wheel policy (synth::ui::ScrollPolicy) ----

// Both ZOOM branches are chosen by their modifiers, so they must read the dominant axis: macOS
// folds Shift+wheel into deltaX, which used to leave Cmd+Shift+wheel reading deltaY == 0 and doing
// nothing at all.
TEST(TimelinePanelInteractionTest, ModifierZoomSurvivesTheShiftAxisSwap) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 220);
    for (int i = 0; i < 24; ++i)
        doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(i));

    auto& state = panel.getViewState();
    state.pixelsPerBeat = 24.0;
    state.firstVisibleBeat = 500.0;
    state.rowHeightScale = 1.0;

    // Cmd + a deltaX-ONLY wheel: horizontal zoom still happens.
    juce::MouseWheelDetails xOnly{};
    xOnly.deltaX = 0.5f;
    panel.mouseWheelMove(
        makeClickEvent(panel, {300.0f, 12.0f}, juce::ModifierKeys(juce::ModifierKeys::commandModifier)), xOnly);
    EXPECT_GT(state.pixelsPerBeat, 24.0);

    // Cmd+Shift + a deltaX-only wheel: the row-height zoom that the axis swap used to kill.
    panel.mouseWheelMove(
        makeClickEvent(panel, {300.0f, 100.0f},
                       juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier)),
        xOnly);
    EXPECT_GT(state.rowHeightScale, 1.0);
}

// Plain scroll follows juce::Viewport's sign convention (origin -= delta) by default, and
// setScrollInverted(true) flips both axes by exactly the same amount.
TEST(TimelinePanelInteractionTest, ScrollInversionFlipsBothAxesAroundTheViewportConvention) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 220);
    for (int i = 0; i < 24; ++i)
        doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(i));

    auto& state = panel.getViewState();
    state.pixelsPerBeat = 24.0;
    ASSERT_FALSE(panel.isScrollInverted()) << "natural is the default";

    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 0.5f;
    const auto shift = juce::ModifierKeys(juce::ModifierKeys::shiftModifier);

    // Horizontal (Shift+wheel). Start well clear of the firstVisibleBeat >= 0 clamp so both
    // directions have room.
    state.firstVisibleBeat = 500.0;
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}, shift), wheel);
    const double naturalBeat = state.firstVisibleBeat;
    EXPECT_LT(naturalBeat, 500.0);

    state.firstVisibleBeat = 500.0;
    panel.setScrollInverted(true);
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}, shift), wheel);
    EXPECT_GT(state.firstVisibleBeat, 500.0);
    EXPECT_NEAR(state.firstVisibleBeat - 500.0, 500.0 - naturalBeat, 1e-9) << "same distance, opposite sign";

    // Vertical (plain wheel). Park mid-range so neither direction is swallowed by a clamp.
    panel.setScrollInverted(false);
    state.trackScrollY = 200.0;
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}), wheel);
    const double naturalY = state.trackScrollY;
    EXPECT_LT(naturalY, 200.0);

    state.trackScrollY = 200.0;
    panel.setScrollInverted(true);
    panel.mouseWheelMove(makeClickEvent(panel, {400.0f, 100.0f}), wheel);
    EXPECT_GT(state.trackScrollY, 200.0);
    EXPECT_NEAR(state.trackScrollY - 200.0, 200.0 - naturalY, 1e-9);
}

// ---- Zoom-scroll direction (synth::ui::wheelGestureIsUpward) ----
//
// UP ZOOMS IN by default, and that must hold under BOTH natural-scrolling conventions: JUCE's
// isReversed flag flips which raw delta sign is "up" (see ScrollPolicy.h's wheelGestureIsUpward
// comment), so a raw-delta-sign zoom — what mouseWheelMove did before this test was added — would
// silently reverse itself depending on the OS's natural-scrolling setting. None of the OLDER
// zoom-wheel tests above (WheelScrollsAndCmdWheelZooms, ModifierZoomSurvivesTheShiftAxisSwap) had
// to change for this: they only ever drive a positive deltaY with isReversed left at its default
// (false), which is "up" under both the old raw-sign reading and the new gesture-based one, and
// none of them assert a direction anyway — WheelScrollsAndCmdWheelZooms asserts the anchor
// invariant, ModifierZoomSurvivesTheShiftAxisSwap asserts the axis-swap guard fires at all.
TEST(TimelinePanelInteractionTest, ZoomUpZoomsInOnBothNaturalScrollConventions) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 220);
    for (int i = 0; i < 24; ++i)
        doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(i));

    auto& state = panel.getViewState();
    ASSERT_FALSE(panel.isZoomScrollInverted()) << "up-zooms-in is the default";
    const auto cmd = juce::ModifierKeys(juce::ModifierKeys::commandModifier);
    const auto cmdShift = juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
    const juce::Point<float> cursor(300.0f, 12.0f);

    // isReversed == false: physically "up" is a POSITIVE delta.
    state.pixelsPerBeat = 24.0;
    state.rowHeightScale = 1.0;
    juce::MouseWheelDetails naturalUp{};
    naturalUp.deltaY = 0.5f; // isReversed defaults false
    panel.mouseWheelMove(makeClickEvent(panel, cursor, cmd), naturalUp);
    EXPECT_GT(state.pixelsPerBeat, 24.0) << "up zooms IN horizontally";
    panel.mouseWheelMove(makeClickEvent(panel, cursor, cmdShift), naturalUp);
    EXPECT_GT(state.rowHeightScale, 1.0) << "up zooms IN vertically";

    // isReversed == true: the SAME physical gesture now arrives as a NEGATIVE delta, so the raw
    // sign flipped — but the outcome must not: it is still an upward gesture, so it still zooms IN.
    state.pixelsPerBeat = 24.0;
    state.rowHeightScale = 1.0;
    juce::MouseWheelDetails reversedUp{};
    reversedUp.deltaY = -0.5f;
    reversedUp.isReversed = true;
    panel.mouseWheelMove(makeClickEvent(panel, cursor, cmd), reversedUp);
    EXPECT_GT(state.pixelsPerBeat, 24.0) << "the same physical 'up' zooms IN regardless of isReversed";
    panel.mouseWheelMove(makeClickEvent(panel, cursor, cmdShift), reversedUp);
    EXPECT_GT(state.rowHeightScale, 1.0);
}

// setZoomScrollInverted(true) flips the sense of BOTH zoom axes at once — one preference behind
// both Cmd branches, not two — while leaving the magnitude/sensitivity curve (and the unrelated
// setScrollInverted preference) alone.
TEST(TimelinePanelInteractionTest, SetZoomScrollInvertedFlipsBothZoomAxes) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 220);
    for (int i = 0; i < 24; ++i)
        doc.addTrack(synth::TrackKind::Midi, "T" + juce::String(i));

    auto& state = panel.getViewState();
    const auto cmd = juce::ModifierKeys(juce::ModifierKeys::commandModifier);
    const auto cmdShift = juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
    const juce::Point<float> cursor(300.0f, 12.0f);
    juce::MouseWheelDetails up{};
    up.deltaY = 0.5f;

    panel.setZoomScrollInverted(true);
    EXPECT_TRUE(panel.isZoomScrollInverted());

    state.pixelsPerBeat = 24.0;
    state.rowHeightScale = 1.0;
    panel.mouseWheelMove(makeClickEvent(panel, cursor, cmd), up);
    EXPECT_LT(state.pixelsPerBeat, 24.0) << "inverted: up now zooms OUT horizontally";
    panel.mouseWheelMove(makeClickEvent(panel, cursor, cmdShift), up);
    EXPECT_LT(state.rowHeightScale, 1.0) << "inverted: up now zooms OUT vertically";

    // A separate preference — flipping zoom must not have touched plain-scroll direction.
    EXPECT_FALSE(panel.isScrollInverted());
}

// The panel forwards BOTH scroll-direction preferences to the piano roll: it runs its OWN
// plain-scroll and Cmd-wheel-zoom branches (PianoRollComponent::mouseWheelMove), so a preference
// set from the panel chrome (or Preferences) has to reach it directly — TimelineViewState, shared
// between the two surfaces, carries no scroll/zoom preferences to piggyback on.
TEST(TimelinePanelInteractionTest, ScrollAndZoomInversionForwardToThePianoRoll) {
    synth::ui::TimelinePanelComponent panel;
    ASSERT_FALSE(panel.getPianoRoll().isScrollInverted());
    ASSERT_FALSE(panel.getPianoRoll().isZoomScrollInverted());

    panel.setScrollInverted(true);
    EXPECT_TRUE(panel.getPianoRoll().isScrollInverted());

    panel.setZoomScrollInverted(true);
    EXPECT_TRUE(panel.getPianoRoll().isZoomScrollInverted());

    panel.setScrollInverted(false);
    panel.setZoomScrollInverted(false);
    EXPECT_FALSE(panel.getPianoRoll().isScrollInverted());
    EXPECT_FALSE(panel.getPianoRoll().isZoomScrollInverted());
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

// ---- "P" on the clip lanes points the transport's loop at the selected clips ----

TEST_F(TimelineAppWiringTest, LoopSelectionKeySetsTransportLoop) {
    MainComponent mc(std::make_unique<MockProviderTL>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& doc = mc.getTimelineDoc();
    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipA = doc.addClip(trackId, 4.0, 4.0, "A");
    const auto clipB = doc.addClip(trackId, 12.0, 2.0, "B");
    ASSERT_TRUE(clipA.isValid());
    ASSERT_TRUE(clipB.isValid());

    auto& panel = mc.getTimelinePanel();
    auto& transport = mc.getAudioEngine().getTransport();
    ASSERT_FALSE(transport.getPositionSnapshot().looping);

    // Nothing selected: the key falls through and the transport is untouched.
    EXPECT_FALSE(panel.getClipLaneArea().keyPressed(juce::KeyPress('p')));
    transport.tick(512);
    EXPECT_FALSE(transport.getPositionSnapshot().looping);

    // Two clips selected -> the loop spans both, and looping is switched ON by the same gesture.
    panel.getClipSelection().setSelection({clipA, clipB});
    EXPECT_TRUE(panel.getClipLaneArea().keyPressed(juce::KeyPress('p')));
    transport.tick(512);

    const auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 4.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 14.0);
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

    // Also the widest band: at kMaxPixelsPerBeat the strip draws beat ticks AND their "bar.beat"
    // sub-labels, so this snapshot covers that paint path too.
    state.pixelsPerBeat = synth::ui::TimelineViewState::kMaxPixelsPerBeat;
    ASSERT_TRUE(synth::ui::rulerTickPlanFor(state.pixelsPerBeat, 4.0).drawBeatLabels);
    const juce::Image zoomedIn = ruler.createComponentSnapshot(ruler.getLocalBounds());
    EXPECT_FALSE(zoomedIn.isNull());
    EXPECT_EQ(zoomedIn.getWidth(), 800);
    EXPECT_EQ(zoomedIn.getHeight(), 24);
}

// The ruler's three density bands, asserted through the pure helper paint() itself calls — no
// painting, no font measurement, so the thresholds mean the same thing on every platform. The SNAP
// division is deliberately absent from all of this: the strip is a bars/beats reference, not a
// picture of the current grid.
TEST(TimelineRulerTickPlanTest, TooDenseForBeatTicksDrawsNeitherTicksNorLabels) {
    const auto plan = synth::ui::rulerTickPlanFor(synth::ui::kMinBeatTickSpacingPx - 0.5, 4.0);
    EXPECT_FALSE(plan.drawBeatTicks);
    EXPECT_FALSE(plan.drawBeatLabels);
    // The threshold itself is inclusive — and is comfortably above the ~6 px the ticks need to read
    // as separate marks.
    EXPECT_GE(synth::ui::kMinBeatTickSpacingPx, 6.0);
    EXPECT_TRUE(synth::ui::rulerTickPlanFor(synth::ui::kMinBeatTickSpacingPx, 4.0).drawBeatTicks);
}

TEST(TimelineRulerTickPlanTest, MiddleBandDrawsTicksWithoutLabels) {
    for (const double pixelsPerBeat :
         {synth::ui::kMinBeatTickSpacingPx, 24.0, synth::ui::kMinBeatLabelSpacingPx - 0.5}) {
        const auto plan = synth::ui::rulerTickPlanFor(pixelsPerBeat, 4.0);
        EXPECT_TRUE(plan.drawBeatTicks) << "pixelsPerBeat " << pixelsPerBeat;
        EXPECT_FALSE(plan.drawBeatLabels) << "pixelsPerBeat " << pixelsPerBeat;
    }
}

TEST(TimelineRulerTickPlanTest, WideBandDrawsTicksAndBarDotBeatLabels) {
    for (const double pixelsPerBeat : {synth::ui::kMinBeatLabelSpacingPx, 200.0}) {
        const auto plan = synth::ui::rulerTickPlanFor(pixelsPerBeat, 4.0);
        EXPECT_TRUE(plan.drawBeatTicks) << "pixelsPerBeat " << pixelsPerBeat;
        EXPECT_TRUE(plan.drawBeatLabels) << "a label always sits against a tick";
    }
}

TEST(TimelineRulerTickPlanTest, ABarOfOneBeatOrLessHasNoBeatsToMark) {
    // Every "beat" would land on a bar line, so a tick there would only thicken it.
    EXPECT_FALSE(synth::ui::rulerTickPlanFor(200.0, 1.0).drawBeatTicks);
    EXPECT_FALSE(synth::ui::rulerTickPlanFor(200.0, 0.0).drawBeatLabels);
    // And a degenerate zoom is inert rather than undefined.
    EXPECT_FALSE(synth::ui::rulerTickPlanFor(0.0, 4.0).drawBeatTicks);
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

// ============================================================================
// Panel-scoped keys (Q = snap toggle, L = loop toggle, P = loop the selection) and the ruler's
// piano-roll mapping override.
// ============================================================================

TEST(TimelinePanelComponentTest, QKeyTogglesSnapEnabled) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1000, 300);
    ASSERT_TRUE(panel.getViewState().snapEnabled);

    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('q')));
    EXPECT_FALSE(panel.getViewState().snapEnabled);
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('q')));
    EXPECT_TRUE(panel.getViewState().snapEnabled);
}

// The transport bar's own Q button flips the same shared switch and mirrors its state — including
// when the flip came from somewhere else (the Q key here).
TEST(TimelinePanelComponentTest, SnapToggleButtonFlipsAndMirrorsSnapEnabled) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1000, 300);
    auto& button = panel.getSnapToggleButton();
    ASSERT_TRUE(panel.getViewState().snapEnabled);
    EXPECT_TRUE(button.getToggleState());

    button.onClick();
    EXPECT_FALSE(panel.getViewState().snapEnabled);
    EXPECT_FALSE(button.getToggleState());

    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('q')));
    EXPECT_TRUE(button.getToggleState()) << "a Q-key flip re-lights the button";
}

TEST(TimelinePanelComponentTest, PickingADivisionReEnablesSnap) {
    synth::ui::TimelinePanelComponent panel;
    panel.getViewState().snapEnabled = false;
    panel.getSnapCombo().setSelectedId(4, juce::sendNotificationSync); // "1/2"
    EXPECT_TRUE(panel.getViewState().snapEnabled) << "choosing a grid size means 'snap to THIS'";
    EXPECT_EQ(panel.getViewState().snap, synth::ui::TimelineViewState::Snap::Half);
}

TEST(TimelinePanelComponentTest, LKeyTogglesLoopingKeepingBounds) {
    synth::ui::TimelinePanelComponent panel;
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    ASSERT_TRUE(transport.setLoop(2.0, 6.0, false));
    transport.tick(512);
    panel.setTransport(&transport);

    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('l')));
    transport.tick(512);
    auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 6.0);

    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('l')));
    transport.tick(512);
    snap = transport.getPositionSnapshot();
    EXPECT_FALSE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0) << "toggling keeps the bounds";
}

TEST(TimelinePanelComponentTest, PKeyLoopsTheSelectedClipsFromPanelScope) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    panel.setTransport(&transport);
    panel.setTimelineDoc(&doc);

    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto clipA = doc.addClip(trackId, 4.0, 4.0, "A");
    const auto clipB = doc.addClip(trackId, 12.0, 2.0, "B");
    ASSERT_TRUE(clipA.isValid());
    ASSERT_TRUE(clipB.isValid());

    EXPECT_FALSE(panel.keyPressed(juce::KeyPress('p'))) << "no selection -> the key falls through";

    panel.getClipSelection().setSelection({clipA, clipB});
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('p')));
    transport.tick(512);
    const auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 4.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 14.0) << "the span covers the whole multi-clip selection";
}

// With the "timelineLoopSelectionArms" preference off, P places the locators but leaves the loop
// switch exactly as it was (Cubase's reading); L is then what arms it.
TEST(TimelinePanelComponentTest, PKeyRespectsTheLoopSelectionArmsPreference) {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth Timeline LoopArms Test";
    opts.folderName = "Agent Synth Timeline LoopArms Test";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    juce::ApplicationProperties props;
    props.setStorageParameters(opts);
    if (auto* s = props.getUserSettings())
        s->setValue("timelineLoopSelectionArms", "0");

    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    panel.setTransport(&transport);
    panel.setTimelineDoc(&doc);
    panel.setApplicationProperties(&props);

    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto clipId = doc.addClip(trackId, 4.0, 4.0, "A");
    ASSERT_TRUE(clipId.isValid());
    panel.getClipSelection().setSelection({clipId});

    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('p')));
    transport.tick(512);
    auto snap = transport.getPositionSnapshot();
    EXPECT_FALSE(snap.looping) << "preference off: locators only, looping untouched";
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 4.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 8.0);

    // Already-armed looping stays armed — the preference suppresses the ARM, it never disarms.
    ASSERT_TRUE(transport.setLoop(0.0, 2.0, true));
    transport.tick(512);
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('p')));
    transport.tick(512);
    snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 4.0);

    if (auto* s = props.getUserSettings())
        s->getFile().deleteFile();
}

TEST(TimelinePanelComponentTest, PKeyWithThePianoRollOpenLoopsTheEditedClip) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    panel.setTransport(&transport);
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 320);

    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto clipId = doc.addClip(trackId, 8.0, 4.0, "Clip");
    ASSERT_TRUE(clipId.isValid());
    panel.openPianoRoll(clipId);
    ASSERT_TRUE(panel.isPianoRollOpen());

    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('p')));
    transport.tick(512);
    const auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 8.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 12.0);
}

// While the roll is open the ruler mirrors the ROLL's mapping (offset by the keys gutter), so its
// bar numbers show the edited clip's real timeline position; closing restores the shared mapping.
TEST(TimelinePanelComponentTest, PianoRollOpenInstallsTheRulerMappingOverride) {
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setSize(1200, 320);

    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto clipId = doc.addClip(trackId, 20.0, 4.0, "Clip"); // bar 6 in 4/4
    ASSERT_TRUE(clipId.isValid());
    ASSERT_FALSE(panel.getRuler().hasMappingOverrideForTest());

    panel.openPianoRoll(clipId);
    ASSERT_TRUE(panel.isPianoRollOpen());
    EXPECT_TRUE(panel.getRuler().hasMappingOverrideForTest());
    // The roll framed the clip with its start at the keys gutter's right edge — beat 20, i.e. the
    // ruler above now starts labelling from bar 6, not bar 1.
    EXPECT_DOUBLE_EQ(panel.getPianoRoll().getFirstVisibleBeat(), 20.0);

    panel.closePianoRoll();
    EXPECT_FALSE(panel.getRuler().hasMappingOverrideForTest());
}

// ============================================================================
// 7. The edit-tool strip + the clip clipboard/arrangement verbs the app's Cut/Copy/Paste/
//    Duplicate/Select All/Repeat commands delegate to. Panel level, so ungated (see the file
//    header). Every fixture sets the view state explicitly — these verbs snap against it.
// ============================================================================

namespace {

struct ToolPanelFixture {
    synth::TimelineDoc doc;
    AppUndoManager undo;
    synth::ui::TimelinePanelComponent panel;

    ToolPanelFixture() {
        panel.setSize(1200, 320);
        auto& state = panel.getViewState();
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = synth::ui::TimelineViewState::Snap::Quarter;
        state.snapEnabled = true;
        state.rowHeightScale = 1.0;
        state.trackScrollY = 0.0;
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        // No transport is wired on purpose: pasteClipsAtPlayhead then reads beat 0, so every
        // pasted position below is arithmetic rather than a transport race.
    }

    void select(std::initializer_list<synth::ClipId> ids) { panel.getClipSelection().setSelection(ids); }
    // Non-const: getClipSelection() only has a non-const overload (the panel owns the model).
    std::vector<synth::ClipId> selection() { return panel.getClipSelection().getSelected(); }
};

} // namespace

// ---- Tool strip + number keys ----

TEST(TimelineToolStripTest, NumberKeysPickToolsAndReservedDigitsFallThrough) {
    ToolPanelFixture f;
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Select) << "Select is the default";

    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('3')));
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Split);
    EXPECT_EQ(f.panel.getClipLaneArea().getActiveTool(), synth::ui::EditTool::Split)
        << "the panel's tool is pushed into the lane area";

    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('8')));
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Draw);

    // 2 (Range), 6 (Zoom) and 9 (Play) are reserved for tools we don't ship: unconsumed, so they
    // keep whatever meaning they have elsewhere, and the active tool is untouched.
    EXPECT_FALSE(f.panel.keyPressed(juce::KeyPress('2')));
    EXPECT_FALSE(f.panel.keyPressed(juce::KeyPress('6')));
    EXPECT_FALSE(f.panel.keyPressed(juce::KeyPress('9')));
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Draw);

    // A command-modified digit belongs to the app's menu shortcuts, never to the tool row.
    EXPECT_FALSE(f.panel.keyPressed(
        juce::KeyPress('1', juce::ModifierKeys(juce::ModifierKeys::commandModifier), juce::juce_wchar('1'))));
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Draw);

    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('1')));
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Select);
}

// The digits are rebindable now: with a ShortcutManager installed they resolve through
// "timelineToolSelect"/"timelineToolSplit"/... instead of synth::ui::editToolForKeyChar. Two halves
// to pin — the rebind takes effect AND the old key stops working, which is the half that silently
// regresses if a fallback creeps back in.
TEST(TimelineToolStripTest, ToolDigitsFollowARebindAndTheOldDigitStopsWorking) {
    ToolPanelFixture f;
    ShortcutManager shortcuts; // defaults only; never loadFromProperties, so nothing is persisted
    f.panel.setShortcutManager(&shortcuts);

    // Baseline: the factory digits still work through the manager.
    ASSERT_TRUE(f.panel.keyPressed(juce::KeyPress('3')));
    ASSERT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Split);
    ASSERT_TRUE(f.panel.keyPressed(juce::KeyPress('1')));
    ASSERT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Select);

    // Move Split onto a letter no other Timeline binding uses.
    shortcuts.setBinding("timelineToolSplit", juce::KeyPress('j', juce::ModifierKeys::noModifiers, 0));

    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('j')));
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Split);

    f.panel.setActiveTool(synth::ui::EditTool::Select);
    EXPECT_FALSE(f.panel.keyPressed(juce::KeyPress('3'))) << "the old digit must fall through, not still pick Split";
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Select);

    // Every OTHER digit is untouched by the one rebind.
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('8')));
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Draw);

    // Clearing a binding means NO key, never a fall back to the factory digit — the strict
    // resolution contract (see TimelinePanelComponent::setShortcutManager).
    shortcuts.setBinding("timelineToolDraw", juce::KeyPress());
    f.panel.setActiveTool(synth::ui::EditTool::Select);
    EXPECT_FALSE(f.panel.keyPressed(juce::KeyPress('8')));
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Select);

    // The Ctrl+Shift+digit grid commands share the tool digits' key codes and must never be mistaken
    // for them — juce::KeyPress equality is exact on modifiers.
    const int ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;
    EXPECT_FALSE(f.panel.keyPressed(juce::KeyPress('1', juce::ModifierKeys(ctrlShift), 0)));
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Select);

    // Detach: with no manager the hardcoded Cubase digits are back, unchanged.
    f.panel.setShortcutManager(nullptr);
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('3')));
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Split);
    EXPECT_FALSE(f.panel.keyPressed(juce::KeyPress('2'))) << "2/6/9 stay reserved on the fallback path";
}

// The panel's three letter keys go through the same resolution. Q is shared with the piano roll
// ("timelineSnapToggle" — one binding, one key, whichever surface has focus).
TEST(TimelineToolStripTest, PanelLetterKeysResolveThroughTheShortcutManager) {
    ToolPanelFixture f;
    ShortcutManager shortcuts;
    f.panel.setShortcutManager(&shortcuts);

    auto& view = f.panel.getViewState();
    const bool snapBefore = view.snapEnabled;
    ASSERT_TRUE(f.panel.keyPressed(juce::KeyPress('q')));
    EXPECT_EQ(view.snapEnabled, !snapBefore);

    shortcuts.setBinding("timelineSnapToggle", juce::KeyPress('y', juce::ModifierKeys::noModifiers, 0));
    EXPECT_FALSE(f.panel.keyPressed(juce::KeyPress('q'))) << "the old key falls through";
    EXPECT_EQ(view.snapEnabled, !snapBefore) << "and did not toggle again";
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('y')));
    EXPECT_EQ(view.snapEnabled, snapBefore);

    // Shift+Q is the ROLL's quantise, a different action: it must not reach the panel's snap toggle
    // (the pre-shortcut code matched Q on the key code alone, ignoring modifiers entirely).
    f.panel.setShortcutManager(nullptr);
    const bool snapNow = view.snapEnabled;
    EXPECT_FALSE(f.panel.keyPressed(juce::KeyPress('q', juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(view.snapEnabled, snapNow);
}

// TimelineClipLaneArea resolves its own P through the SAME action id the panel's fallback uses, so
// the two can never end up on different keys.
TEST(TimelineToolStripTest, ClipLanePLoopSelectionFollowsTheShortcutManager) {
    ToolPanelFixture f;
    ShortcutManager shortcuts;
    auto& lane = f.panel.getClipLaneArea();
    lane.setShortcutManager(&shortcuts);

    const auto track = f.doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip = f.doc.addClip(track, 4.0, 4.0, "C1");
    ASSERT_TRUE(clip.isValid());
    f.select({clip});

    int calls = 0;
    double gotStart = -1.0;
    double gotEnd = -1.0;
    lane.onLoopRangeRequested = [&](double start, double end) {
        ++calls;
        gotStart = start;
        gotEnd = end;
    };

    EXPECT_TRUE(lane.keyPressed(juce::KeyPress('p')));
    EXPECT_EQ(calls, 1);
    EXPECT_DOUBLE_EQ(gotStart, 4.0);
    EXPECT_DOUBLE_EQ(gotEnd, 8.0);

    shortcuts.setBinding("timelineLoopSelection", juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0));
    EXPECT_FALSE(lane.keyPressed(juce::KeyPress('p')));
    EXPECT_EQ(calls, 1) << "the old key must not still fire the callback";
    EXPECT_TRUE(lane.keyPressed(juce::KeyPress('u')));
    EXPECT_EQ(calls, 2);

    // Delete/Escape are FIXED, never resolved through the manager — clearing every binding must not
    // disturb them.
    for (const auto& actionId : shortcuts.getActionIds())
        shortcuts.setBinding(actionId, juce::KeyPress());
    EXPECT_TRUE(lane.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))) << "a non-empty selection consumes Escape";
    f.select({clip});
    EXPECT_TRUE(lane.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(f.doc.getClip(clip), nullptr);
}

TEST(TimelineToolStripTest, ButtonsMirrorTheActiveToolAndCarryTheirShortcutInTheTooltip) {
    ToolPanelFixture f;

    for (auto tool : synth::ui::kAllEditTools)
        ASSERT_NE(f.panel.getToolButton(tool), nullptr) << "every tool has a button, headless included";

    EXPECT_EQ(f.panel.getToolButton(synth::ui::EditTool::Split)->getTooltip(), "Split (3)");
    EXPECT_EQ(f.panel.getToolButton(synth::ui::EditTool::Draw)->getTooltip(), "Draw (8)");

    f.panel.setActiveTool(synth::ui::EditTool::Erase);
    for (auto tool : synth::ui::kAllEditTools)
        EXPECT_EQ(f.panel.getToolButton(tool)->getToggleState(), tool == synth::ui::EditTool::Erase)
            << "exactly one button is lit: " << synth::ui::editToolName(tool);
}

// ---- Keyboard-focus routing (the "Cmd+X works everywhere but the tracks" regression) ----
//
// MainComponent::resolveEditSurface() reads the REAL focused component, so which component owns
// focus is what decides where Cmd+X/C/V/D — and the lane's own Delete/Escape/P — are routed. The
// two guards below pin the two halves of that: the lane can receive focus at all, and no tool
// button takes it away. Neither is observable through the editSurfaceOverrideForTest_ path the
// routing tests use, which is precisely why the hole shipped.

TEST(TimelineToolStripTest, ClipLaneAcceptsKeyboardFocusSoSurfaceVerbsCanRoute) {
    ToolPanelFixture f;
    EXPECT_TRUE(f.panel.getClipLaneArea().getWantsKeyboardFocus())
        << "juce::grabKeyboardFocus() (called from TimelineClipLaneArea::mouseDown) is a no-op "
           "without this, so clicking a clip would leave focus wherever it was and every "
           "per-surface verb would fall through to the graph";
}

TEST(TimelineToolStripTest, ToolButtonsNeverGrabKeyboardFocusFromTheEditSurfaces) {
    ToolPanelFixture f;
    for (auto tool : synth::ui::kAllEditTools) {
        auto* button = f.panel.getToolButton(tool);
        ASSERT_NE(button, nullptr);
        EXPECT_FALSE(button->getWantsKeyboardFocus())
            << "juce::Button opts into focus by default: " << synth::ui::editToolName(tool);
        EXPECT_FALSE(button->getMouseClickGrabsKeyboardFocus())
            << "picking a tool must not move focus off the clip lane, or the next Cmd+X goes to "
               "the graph: "
            << synth::ui::editToolName(tool);
    }
}

// ---- Clipboard: the audio-field regression ----

TEST(TimelineClipClipboardTest, CopyPasteRoundTripsEveryAudioFieldAndTheMuteFlag) {
    ToolPanelFixture f;
    const auto track = f.doc.addTrack(synth::TrackKind::Audio, "Audio 1");
    const auto clip = f.doc.addClip(track, 8.0, 4.0, "Take 1");
    ASSERT_TRUE(clip.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(clip, "Audio/take-1.wav", 1.5));
    ASSERT_TRUE(f.doc.setClipGainDb(clip, -3.5));
    ASSERT_TRUE(f.doc.setClipFades(clip, 0.5, 0.25));
    ASSERT_TRUE(f.doc.setClipMuted(clip, true));

    f.select({clip});
    ASSERT_TRUE(f.panel.copySelectedClips());
    ASSERT_TRUE(f.panel.canPasteClips());
    ASSERT_TRUE(f.panel.pasteClipsAtPlayhead());

    const auto pasted = f.selection();
    ASSERT_EQ(pasted.size(), 1u);
    const auto* copy = f.doc.getClip(pasted[0]);
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy->id, clip);
    EXPECT_DOUBLE_EQ(copy->startBeat, 0.0) << "re-based onto the (transport-less) playhead at beat 0";
    EXPECT_DOUBLE_EQ(copy->lengthBeats, 4.0);
    EXPECT_EQ(copy->name, "Take 1");
    // The regression itself: every one of these used to be dropped, leaving a silent husk.
    EXPECT_EQ(copy->assetRef, "Audio/take-1.wav");
    EXPECT_DOUBLE_EQ(copy->sourceStartSeconds, 1.5);
    EXPECT_DOUBLE_EQ(copy->gainDb, -3.5);
    EXPECT_DOUBLE_EQ(copy->fadeInBeats, 0.5);
    EXPECT_DOUBLE_EQ(copy->fadeOutBeats, 0.25);
    EXPECT_TRUE(copy->muted);
    // And it landed on an AUDIO row, which is the only kind that plays an asset.
    ASSERT_NE(f.doc.getTrackForClip(copy->id), nullptr);
    EXPECT_EQ(f.doc.getTrackForClip(copy->id)->kind, synth::TrackKind::Audio);
}

TEST(TimelineClipClipboardTest, PasteFallsBackToTheFirstTrackOfTheRequiredKind) {
    ToolPanelFixture f;
    const auto midi = f.doc.addTrack(synth::TrackKind::Midi, "Midi 1");
    const auto source = f.doc.addTrack(synth::TrackKind::Audio, "Audio source");
    const auto spare = f.doc.addTrack(synth::TrackKind::Audio, "Audio spare");
    ASSERT_TRUE(midi.isValid() && spare.isValid());
    const auto clip = f.doc.addClip(source, 0.0, 4.0, "Take");
    ASSERT_TRUE(clip.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(clip, "Audio/take-1.wav", 0.0));

    f.select({clip});
    ASSERT_TRUE(f.panel.copySelectedClips());
    ASSERT_TRUE(f.doc.removeTrack(source)); // the original row is gone

    ASSERT_TRUE(f.panel.pasteClipsAtPlayhead());
    ASSERT_EQ(f.doc.getTrack(spare)->clips.size(), 1u)
        << "an audio clip falls back to the first AUDIO track, never to the MIDI one";
    EXPECT_TRUE(f.doc.getTrack(midi)->clips.empty());
    EXPECT_EQ(f.doc.getTrack(spare)->clips[0].assetRef, "Audio/take-1.wav");
}

TEST(TimelineClipClipboardTest, PasteSkipsAClipWithNoRowOfItsKindLeft) {
    ToolPanelFixture f;
    const auto audio = f.doc.addTrack(synth::TrackKind::Audio, "Audio");
    const auto clip = f.doc.addClip(audio, 0.0, 4.0, "Take");
    ASSERT_TRUE(clip.isValid());
    ASSERT_TRUE(f.doc.setClipAsset(clip, "Audio/take-1.wav", 0.0));

    f.select({clip});
    ASSERT_TRUE(f.panel.copySelectedClips());
    ASSERT_TRUE(f.doc.removeTrack(audio));
    ASSERT_TRUE(f.doc.addTrack(synth::TrackKind::Midi, "Midi only").isValid());

    EXPECT_FALSE(f.panel.pasteClipsAtPlayhead()) << "nowhere it could play: skipped, not parked on the MIDI row";
    EXPECT_TRUE(f.doc.getTracks()[0].clips.empty());
}

TEST(TimelineClipClipboardTest, NoteMuteFlagsSurviveCopyPaste) {
    ToolPanelFixture f;
    const auto track = f.doc.addTrack(synth::TrackKind::Midi, "Midi");
    const auto clip = f.doc.addClip(track, 4.0, 4.0, "Riff");
    ASSERT_TRUE(clip.isValid());

    synth::MidiNote audible;
    audible.startBeat = 0.0;
    audible.pitch = 60;
    synth::MidiNote silenced;
    silenced.startBeat = 1.0;
    silenced.pitch = 64;
    const auto audibleId = f.doc.addNote(clip, audible);
    const auto silencedId = f.doc.addNote(clip, silenced);
    ASSERT_TRUE(audibleId.isValid() && silencedId.isValid());
    ASSERT_TRUE(f.doc.setNoteMuted(silencedId, true));

    f.select({clip});
    ASSERT_TRUE(f.panel.copySelectedClips());
    ASSERT_TRUE(f.panel.pasteClipsAtPlayhead());

    const auto pasted = f.selection();
    ASSERT_EQ(pasted.size(), 1u);
    const auto* copy = f.doc.getClip(pasted[0]);
    ASSERT_NE(copy, nullptr);
    ASSERT_EQ(copy->notes.size(), 2u);
    EXPECT_NE(copy->notes[0].id, audibleId) << "pasted notes get fresh ids";
    EXPECT_FALSE(copy->notes[0].muted);
    EXPECT_TRUE(copy->notes[1].muted) << "a note's mute is part of the note, so it survives the clipboard";
}

// ---- Cut / Select All / Repeat ----

TEST(TimelineClipVerbsTest, CutRemovesTheSelectionInOneStepAndLeavesItPasteable) {
    ToolPanelFixture f;
    const auto track = f.doc.addTrack(synth::TrackKind::Midi, "Midi");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "a");
    const auto b = f.doc.addClip(track, 8.0, 4.0, "b");
    ASSERT_TRUE(a.isValid() && b.isValid());

    EXPECT_FALSE(f.panel.canCutClips()) << "nothing selected: nothing to cut";
    f.select({a, b});
    EXPECT_TRUE(f.panel.canCutClips());
    EXPECT_TRUE(f.panel.hasClipSelection());

    ASSERT_TRUE(f.panel.cutSelectedClips());
    EXPECT_TRUE(f.doc.getTrack(track)->clips.empty());
    EXPECT_TRUE(f.panel.canPasteClips()) << "a cut fills the clipboard — that is what makes it a cut";
    EXPECT_FALSE(f.panel.hasClipSelection());

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 2u) << "both clips came back in ONE undo";

    // And the clipboard survived the cut, so the pasted pair keeps its relative spacing.
    ASSERT_TRUE(f.panel.pasteClipsAtPlayhead());
    const auto pasted = f.selection();
    ASSERT_EQ(pasted.size(), 2u);
    EXPECT_DOUBLE_EQ(f.doc.getClip(pasted[0])->startBeat, 0.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(pasted[1])->startBeat, 8.0);
}

TEST(TimelineClipVerbsTest, SelectAllSelectsEveryClipOnEveryTrack) {
    ToolPanelFixture f;
    const auto midi = f.doc.addTrack(synth::TrackKind::Midi, "Midi");
    const auto audio = f.doc.addTrack(synth::TrackKind::Audio, "Audio");
    ASSERT_TRUE(f.doc.addClip(midi, 0.0, 4.0, "a").isValid());
    ASSERT_TRUE(f.doc.addClip(midi, 8.0, 4.0, "b").isValid());
    ASSERT_TRUE(f.doc.addClip(audio, 2.0, 4.0, "c").isValid());

    ASSERT_TRUE(f.panel.selectAllClips());
    EXPECT_EQ(f.panel.getClipSelection().size(), 3);

    // An empty arrangement has nothing to select and says so.
    ToolPanelFixture empty;
    ASSERT_TRUE(empty.doc.addTrack(synth::TrackKind::Midi, "Midi").isValid());
    EXPECT_FALSE(empty.panel.selectAllClips());
}

TEST(TimelineClipVerbsTest, RepeatTilesTheSelectionBlockInOneUndoStep) {
    ToolPanelFixture f;
    const auto track = f.doc.addTrack(synth::TrackKind::Midi, "Midi");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "bar");
    ASSERT_TRUE(clip.isValid());
    f.select({clip});

    ASSERT_TRUE(f.panel.repeatSelectedClips(3));

    const auto& clips = f.doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 4u);
    EXPECT_DOUBLE_EQ(clips[0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(clips[1].startBeat, 4.0);
    EXPECT_DOUBLE_EQ(clips[2].startBeat, 8.0);
    EXPECT_DOUBLE_EQ(clips[3].startBeat, 12.0);
    EXPECT_EQ(f.panel.getClipSelection().size(), 3) << "the copies end up selected, not the source";
    EXPECT_FALSE(f.panel.getClipSelection().contains(clip));

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u) << "three copies, ONE undo step";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipVerbsTest, RepeatKeepsAMultiClipBlockIntact) {
    ToolPanelFixture f;
    const auto track = f.doc.addTrack(synth::TrackKind::Midi, "Midi");
    const auto a = f.doc.addClip(track, 0.0, 2.0, "a");
    const auto b = f.doc.addClip(track, 4.0, 2.0, "b"); // block spans [0, 6)
    ASSERT_TRUE(a.isValid() && b.isValid());
    f.select({a, b});

    ASSERT_TRUE(f.panel.repeatSelectedClips(1));

    const auto& clips = f.doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 4u);
    EXPECT_DOUBLE_EQ(clips[2].startBeat, 6.0) << "the whole block moves by its span, keeping its internal spacing";
    EXPECT_DOUBLE_EQ(clips[3].startBeat, 10.0);
}

TEST(TimelineClipVerbsTest, RepeatRejectsANonPositiveCountAndAnEmptySelection) {
    ToolPanelFixture f;
    const auto track = f.doc.addTrack(synth::TrackKind::Midi, "Midi");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "bar");
    ASSERT_TRUE(clip.isValid());

    EXPECT_FALSE(f.panel.repeatSelectedClips(2)) << "nothing selected";
    f.select({clip});
    EXPECT_FALSE(f.panel.repeatSelectedClips(0));
    EXPECT_FALSE(f.panel.repeatSelectedClips(-1));
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
}

// ============================================================================
// 8. Follow-playhead: the toggle (state + button mirror + persistence) and the page-flip it
//    drives inside updateFromTransport(). Panel level, ungated (see the file header) — backfilled
//    for the already-landed TL implementation (see EdgeAutoScroll.h / TimelineClipLaneArea's
//    beat-anchored drag, covered in TimelineClipLaneTests.cpp).
// ============================================================================

namespace {
// Same isolated-properties-file idiom as TimelinePanelSnapComboTest::SnapChoicePersists above:
// hermetic regardless of a previous run, and cleaned up on the way out.
struct IsolatedPropsGuard {
    juce::PropertiesFile::Options opts;
    juce::ApplicationProperties props;

    explicit IsolatedPropsGuard(const char* name) {
        opts.applicationName = name;
        opts.folderName = name;
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        {
            juce::ApplicationProperties initial;
            initial.setStorageParameters(opts);
            if (auto* s = initial.getUserSettings())
                s->getFile().deleteFile();
        }
        props.setStorageParameters(opts);
    }

    ~IsolatedPropsGuard() {
        if (auto* s = props.getUserSettings())
            s->getFile().deleteFile();
    }
};
} // namespace

TEST(TimelineFollowPlayheadTest, ToggleFlipsStateAndButtonMirror) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);
    ASSERT_FALSE(panel.isFollowPlayheadEnabled()) << "documented default: off";
    EXPECT_FALSE(panel.getFollowPlayheadButtonForTest().getToggleState());

    panel.setFollowPlayheadEnabled(true);
    EXPECT_TRUE(panel.isFollowPlayheadEnabled());
    EXPECT_TRUE(panel.getFollowPlayheadButtonForTest().getToggleState()) << "the button only mirrors the state";

    panel.setFollowPlayheadEnabled(false);
    EXPECT_FALSE(panel.isFollowPlayheadEnabled());
    EXPECT_FALSE(panel.getFollowPlayheadButtonForTest().getToggleState());
}

TEST(TimelineFollowPlayheadTest, ButtonClickRoundTripsThroughSetFollowPlayheadEnabled) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);
    ASSERT_FALSE(panel.isFollowPlayheadEnabled());

    panel.getFollowPlayheadButtonForTest().onClick();
    EXPECT_TRUE(panel.isFollowPlayheadEnabled());
    EXPECT_TRUE(panel.getFollowPlayheadButtonForTest().getToggleState());

    panel.getFollowPlayheadButtonForTest().onClick();
    EXPECT_FALSE(panel.isFollowPlayheadEnabled());
    EXPECT_FALSE(panel.getFollowPlayheadButtonForTest().getToggleState());
}

// The choice persists under "timelineFollowPlayhead" and is restored by a fresh
// setApplicationProperties call against the same (isolated) properties file.
TEST(TimelineFollowPlayheadTest, ChoicePersistsAndIsRestored) {
    IsolatedPropsGuard guard("Agent Synth Timeline Follow Test");

    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);
    panel.setApplicationProperties(&guard.props);
    ASSERT_FALSE(panel.isFollowPlayheadEnabled()) << "documented default, freshly-deleted file";

    panel.setFollowPlayheadEnabled(true);
    ASSERT_NE(guard.props.getUserSettings(), nullptr);
    EXPECT_TRUE(guard.props.getUserSettings()->getBoolValue("timelineFollowPlayhead", false));

    synth::ui::TimelinePanelComponent panel2;
    panel2.setSize(1200, 320);
    panel2.setApplicationProperties(&guard.props);
    EXPECT_TRUE(panel2.isFollowPlayheadEnabled());
    EXPECT_TRUE(panel2.getFollowPlayheadButtonForTest().getToggleState());
}

namespace {
// A minimal fixture for updateFromTransport()'s page-flip: a doc-less panel (the page-flip needs
// no TimelineDoc at all) with the view state pinned so the expected math below is exact.
struct FollowPlayheadFixture {
    synth::ui::TimelinePanelComponent panel;

    FollowPlayheadFixture() {
        panel.setSize(1200, 320);
        auto& state = panel.getViewState();
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        panel.setFollowPlayheadEnabled(true);
    }

    // The exact width the page-flip's visibleBeats term reads from — clipLaneArea_ fills
    // gridLanesBounds_ exactly (see TimelinePanelComponent::resized()'s comment), so this IS that
    // width without duplicating layout maths.
    double visibleBeats() {
        return (double)panel.getClipLaneArea().getWidth() / panel.getViewState().pixelsPerBeat;
    }

    synth::TransportService::PositionSnapshot playingSnapshotAt(double ppq) const {
        synth::TransportService::PositionSnapshot snap;
        snap.playing = true;
        snap.ppq = ppq;
        return snap;
    }
};
} // namespace

TEST(TimelineFollowPlayheadTest, PageFlipsWhenThePlayheadCrossesTheRightEdge) {
    FollowPlayheadFixture f;
    const double visible = f.visibleBeats();
    // Just past the last visible beat.
    const double playheadBeat = visible + 1.0;

    f.panel.updateFromTransport(f.playingSnapshotAt(playheadBeat), 0.0);

    // The landed implementation's exact formula: max(0, playheadBeat - 0.1 * visibleBeats).
    const double expected = std::max(0.0, playheadBeat - 0.1 * visible);
    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, expected);
}

TEST(TimelineFollowPlayheadTest, NoScrollWhilePlayheadStaysInsideTheVisibleRange) {
    FollowPlayheadFixture f;
    const double visible = f.visibleBeats();
    const double playheadBeat = visible * 0.5; // comfortably inside [0, visible]

    f.panel.updateFromTransport(f.playingSnapshotAt(playheadBeat), 0.0);

    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0);
}

TEST(TimelineFollowPlayheadTest, NoScrollWhenStopped) {
    FollowPlayheadFixture f;
    const double visible = f.visibleBeats();
    auto snapshot = f.playingSnapshotAt(visible + 1.0);
    snapshot.playing = false;

    f.panel.updateFromTransport(snapshot, 0.0);

    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0);
}

TEST(TimelineFollowPlayheadTest, NoScrollWhenFollowIsOff) {
    FollowPlayheadFixture f;
    f.panel.setFollowPlayheadEnabled(false);
    const double visible = f.visibleBeats();

    f.panel.updateFromTransport(f.playingSnapshotAt(visible + 1.0), 0.0);

    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0);
}

TEST(TimelineFollowPlayheadTest, NoScrollWhileThePianoRollIsOpen) {
    FollowPlayheadFixture f;
    synth::TimelineDoc doc;
    f.panel.setTimelineDoc(&doc);
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Midi");
    const auto clip = doc.addClip(track, 0.0, 4.0, "Clip");
    ASSERT_TRUE(clip.isValid());

    f.panel.openPianoRoll(clip);
    ASSERT_TRUE(f.panel.isPianoRollOpen());

    const double visible = f.visibleBeats();
    f.panel.updateFromTransport(f.playingSnapshotAt(visible + 1.0), 0.0);

    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0);
}

namespace {
// A local left-button mouse event for the clip-lane area — the same shape
// TimelineClipLaneTests.cpp's makeClipMouseEvent builds, kept local here since this file's own
// makeTimelineMouseEvent (above, in the ruler-interaction tests' anonymous namespace) isn't visible
// this far down.
juce::MouseEvent leftButtonEventOnLane(juce::Component& comp, juce::Point<float> pos, juce::Point<float> anchor,
                                       bool wasDragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), anchor, juce::Time::getCurrentTime(), 1,
                            wasDragged);
}
} // namespace

TEST(TimelineFollowPlayheadTest, NoScrollWhileAClipDragIsInProgress) {
    FollowPlayheadFixture f;
    synth::TimelineDoc doc;
    AppUndoManager undo;
    f.panel.setTimelineDoc(&doc);
    f.panel.setUndoManager(&undo);
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Midi");
    const auto clip = doc.addClip(track, 0.0, 4.0, "Clip");
    ASSERT_TRUE(clip.isValid());

    auto& lane = f.panel.getClipLaneArea();
    const auto rect = lane.getClipRect(clip);
    const juce::Point<float> anchor((float)rect.getCentreX(), (float)rect.getCentreY());
    const juce::Point<float> dragged(anchor.x + 10.0f, anchor.y);

    lane.mouseDown(leftButtonEventOnLane(lane, anchor, anchor, false));
    lane.mouseDrag(leftButtonEventOnLane(lane, dragged, anchor, true));
    ASSERT_TRUE(lane.isDragInProgress());

    const double visible = f.visibleBeats();
    f.panel.updateFromTransport(f.playingSnapshotAt(visible + 1.0), 0.0);

    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0);

    lane.mouseUp(leftButtonEventOnLane(lane, dragged, anchor, true));
}
