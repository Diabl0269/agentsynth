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

// TL5-3 adds a fourth group:
//   4. Track headers + the app-level timeline wiring MainComponent owns (publish-on-mutation, the
//      compound add-track flow, reconciliation after an undo/redo restore, .agsproj round trips,
//      recorder wiring). The panel-level header tests are ungated like groups 1–3; everything that
//      needs MainComponent's wiring is #if SYNTH_ENABLE_TIMELINE, since that wiring compiles out.
//      The header ROW itself is covered separately, against a stub host, in
//      TimelineTrackHeaderTests.cpp.

#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIStateMapper.h"
#include "../Source/ProjectBundle.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Transport/TransportService.h"
#include "../Source/UI/TimelinePanelComponent.h"
#include "../Source/UI/TrackColour.h"
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

// ============================================================================
// 4. TL5-3: track headers + the app-level timeline wiring.
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
