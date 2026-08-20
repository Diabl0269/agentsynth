// One focus-ownership rule (MainComponent::resolveEditSurface) for Cmd+C/V/D, Space play/stop, and
// per-surface Delete — one test per verb x surface (see MainComponent.h's
// EditSurface/resolveEditSurface comment and docs/shortcuts.md for the production rule this pins).
//
// Headless/deterministic constraints for the tests that read live transport state:
// AudioEngine::HostMode::Hosted, driven only through prepareForHost/processHostBlock — same house
// rule PluginProcessorTests.cpp/TimelineE2ETests.cpp already follow. Every other test uses
// MainComponent's plain delegating ctor, the same pattern MainComponentTests.cpp uses throughout.

#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIStateMapper.h"
#include "../Source/AudioEngine.h"
#include "../Source/MainComponent.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/UI/Theme/AppLookAndFeel.h"
#include "../Source/UI/Theme/ThemeManager.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace {

// automateParameter()'s toggle path (and, in principle, any test that ever called
// simulateToggleTimelineClick()) persists "timelinePanelVisible" to the SAME on-disk properties
// file every MainComponent instance reads at construction — reset it before AND after every test
// in this file so SurfaceResolverRealFocus's "the panel starts hidden" precondition never depends
// on what ran before it in the same process. Mirrors AutomationEditorTests.cpp's helper of the
// same name exactly.
void resetTimelinePanelVisibleKey() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth";
    opts.folderName = "Agent Synth";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;

    juce::ApplicationProperties props;
    props.setStorageParameters(opts);
    if (auto* s = props.getUserSettings()) {
        s->setValue("timelinePanelVisible", "0");
        s->saveIfNeeded();
    }
}

// A provider that never touches the network — this file only exercises the graph/timeline/command
// plumbing, never the AI chat itself. Mirrors NullAIProvider (PluginProcessorTests.cpp) exactly.
class FocusMockProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "FocusMock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        if (callback)
            callback({}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        if (callback)
            callback(AIResponse{false, {}, {}, {}});
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }
};

} // namespace

class FocusArbitrationTest : public ::testing::Test {
protected:
    void SetUp() override { resetTimelinePanelVisibleKey(); }
    void TearDown() override { resetTimelinePanelVisibleKey(); }
};

// ============================================================================
// 1. Graph surface — unchanged behaviour
// ============================================================================

TEST_F(FocusArbitrationTest, CopyPasteDuplicateOnGraphUnchanged) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);

    auto& editor = mc.getGraphEditor();
    auto& cm = mc.getCommandManager();
    auto& graph = mc.getAudioEngine().getGraph();

    editor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("Oscillator"), &editor, {200, 200}));
    editor.selectAllModules();
    ASSERT_GT(editor.getSelectionCount(), 0);
    const int afterOneModule = graph.getNumNodes();

    // asynchronously = false: the async path posts a CommandMessage and needs a message loop.
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::copySelection, false));
    EXPECT_TRUE(editor.canPaste());

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::pasteSelection, false));
    EXPECT_GT(graph.getNumNodes(), afterOneModule);

    const int afterPaste = graph.getNumNodes();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::duplicateSelection, false));
    EXPECT_GT(graph.getNumNodes(), afterPaste);
}

#if SYNTH_ENABLE_TIMELINE

// ============================================================================
// 2. TimelineClips surface — copy/paste rebased at the (snapped) playhead
// ============================================================================

TEST_F(FocusArbitrationTest, CopyPasteClipsRebasedAtPlayhead) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<FocusMockProvider>());

    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto trackB = doc.addTrack(synth::TrackKind::Midi, "B");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    doc.addNote(clip1, synth::MidiNote{{}, 1.0, 1.0, 60, 100, 1});
    const auto clip2 = doc.addClip(trackB, 8.0, 2.0, "C2");
    ASSERT_TRUE(clip1.isValid());
    ASSERT_TRUE(clip2.isValid());

    auto& clipSelection = mc.getTimelinePanel().getClipSelection();
    clipSelection.setSelection({clip1, clip2});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);

    // Locate the transport, then drive one deterministic host block so the LocateBeat command
    // drains and the position snapshot reflects it — see the file header's Hosted-mode rule.
    auto& transport = engine.getTransport();
    transport.locateBeat(20.25);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    engine.processHostBlock(buffer, midi);
    ASSERT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 20.25);

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::copySelection, false));
    EXPECT_TRUE(mc.getTimelinePanel().canPasteClips());

    const int clipCountBefore = (int)doc.getTrack(trackA)->clips.size() + (int)doc.getTrack(trackB)->clips.size();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::pasteSelection, false));

    const auto pasted = clipSelection.getSelected();
    ASSERT_EQ(pasted.size(), 2u) << "pasted clips end up selected";

    // Default snap is Beat (1.0 beat) at the transport's default 4/4 -> snapBeat(20.25, 4) == 20.0.
    const double expectedEarliest = mc.getTimelinePanel().getViewState().snapBeat(20.25, 4.0);
    ASSERT_DOUBLE_EQ(expectedEarliest, 20.0);

    std::vector<double> starts;
    for (auto id : pasted)
        starts.push_back(doc.getClip(id)->startBeat);
    std::sort(starts.begin(), starts.end());
    EXPECT_DOUBLE_EQ(starts[0], expectedEarliest) << "earliest pasted clip lands at the snapped playhead";
    EXPECT_DOUBLE_EQ(starts[1], expectedEarliest + 8.0) << "relative offset (8 beats) preserved";

    // Landed back on their ORIGINAL tracks.
    bool sawTrackA = false, sawTrackB = false;
    for (auto id : pasted) {
        const auto* track = doc.getTrackForClip(id);
        ASSERT_NE(track, nullptr);
        sawTrackA |= track->id == trackA;
        sawTrackB |= track->id == trackB;
        // The clip that landed on trackA is the C1 copy and must carry its note through.
        if (track->id == trackA) {
            const auto* clip = doc.getClip(id);
            ASSERT_EQ(clip->notes.size(), 1u);
            EXPECT_DOUBLE_EQ(clip->notes[0].startBeat, 1.0);
        }
    }
    EXPECT_TRUE(sawTrackA);
    EXPECT_TRUE(sawTrackB);

    const int clipCountAfter = (int)doc.getTrack(trackA)->clips.size() + (int)doc.getTrack(trackB)->clips.size();
    EXPECT_EQ(clipCountAfter, clipCountBefore + 2);

    // ONE undo step removes both pasted clips together.
    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    const int clipCountAfterUndo = (int)doc.getTrack(trackA)->clips.size() + (int)doc.getTrack(trackB)->clips.size();
    EXPECT_EQ(clipCountAfterUndo, clipCountBefore) << "one Cmd+Z must remove the whole pasted group";
}

// ============================================================================
// 3. TimelineClips surface — missing-track fallback
// ============================================================================

TEST_F(FocusArbitrationTest, PasteWithMissingTrackFallsBack) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto trackB = doc.addTrack(synth::TrackKind::Midi, "B");
    const auto clipOnB = doc.addClip(trackB, 4.0, 2.0, "OnB");
    ASSERT_TRUE(clipOnB.isValid());

    auto& clipSelection = mc.getTimelinePanel().getClipSelection();
    clipSelection.setSelection({clipOnB});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::copySelection, false));

    // The clip's original track disappears before the paste.
    ASSERT_TRUE(doc.removeTrack(trackB));
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::pasteSelection, false));

    const auto pasted = clipSelection.getSelected();
    ASSERT_EQ(pasted.size(), 1u) << "falls back to the remaining MIDI track rather than being dropped";
    const auto* fallbackTrack = doc.getTrackForClip(pasted[0]);
    ASSERT_NE(fallbackTrack, nullptr);
    EXPECT_EQ(fallbackTrack->id, trackA);

    // Now there is NO Midi track left at all — the clip must be skipped outright.
    ASSERT_TRUE(doc.removeTrack(trackA));
    ASSERT_TRUE(doc.getTracks().empty());
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::pasteSelection, false));
    EXPECT_TRUE(doc.getTracks().empty()) << "nothing to fall back to -> the clip is skipped, not misplaced";
}

// ============================================================================
// 4. TimelineClips surface — duplicate, one undo step
// ============================================================================

TEST_F(FocusArbitrationTest, DuplicateClipsOneStep) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    const auto clip2 = doc.addClip(trackA, 8.0, 4.0, "C2");
    ASSERT_TRUE(clip1.isValid());
    ASSERT_TRUE(clip2.isValid());

    auto& clipSelection = mc.getTimelinePanel().getClipSelection();
    clipSelection.setSelection({clip1, clip2});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::duplicateSelection, false));

    const auto selected = clipSelection.getSelected();
    ASSERT_EQ(selected.size(), 2u) << "the new copies end up selected";
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 4u);

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 2u) << "one undo step removes both duplicates together";
}

// ============================================================================
// 5. PianoRoll surface — C/V/D deliberately inactive (v1 gap)
// ============================================================================

TEST_F(FocusArbitrationTest, PianoRollSurfaceCVDInactive) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    mc.getTimelinePanel().getClipSelection().setSelection({clip1});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);

    for (auto cmdId : {AppCommands::copySelection, AppCommands::pasteSelection, AppCommands::duplicateSelection}) {
        juce::ApplicationCommandInfo info(cmdId);
        mc.getCommandInfo(cmdId, info);
        EXPECT_NE(info.flags & juce::ApplicationCommandInfo::isDisabled, 0)
            << "command " << cmdId << " must be inactive on the piano-roll surface";
    }

    auto& cm = mc.getCommandManager();
    const auto revisionBefore = doc.getRevision();
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::copySelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::pasteSelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::duplicateSelection, false));
    EXPECT_EQ(doc.getRevision(), revisionBefore) << "an inactive command must never touch the doc";
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 1u);
}

#endif // SYNTH_ENABLE_TIMELINE

// ============================================================================
// 6. Space — global play/stop toggle
// ============================================================================

TEST_F(FocusArbitrationTest, SpaceTogglesPlayback) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<FocusMockProvider>());
    auto& cm = mc.getCommandManager();

#if SYNTH_ENABLE_TIMELINE
    auto& transport = engine.getTransport();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    // Works from any surface override — Space is deliberately NOT routed by resolveEditSurface().
    for (auto surface : {MainComponent::EditSurface::Graph, MainComponent::EditSurface::TimelineClips,
                         MainComponent::EditSurface::PianoRoll}) {
        mc.setEditSurfaceOverrideForTest(surface);

        engine.processHostBlock(buffer, midi);
        ASSERT_FALSE(transport.getPositionSnapshot().playing);

        // perform() reuses the transport bar's play/stop button, and juce::Button::triggerClick()
        // always POSTS its click (never fires onClick synchronously) — pump the message loop
        // briefly so it actually runs, the same idiom GraphEditorTests.cpp's setKnobs() helper uses
        // for a marshalled callback, before draining the resulting transport command with a tick.
        ASSERT_TRUE(cm.invokeDirectly(AppCommands::togglePlayback, false));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        engine.processHostBlock(buffer, midi);
        EXPECT_TRUE(transport.getPositionSnapshot().playing) << "surface " << (int)surface;

        ASSERT_TRUE(cm.invokeDirectly(AppCommands::togglePlayback, false));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        engine.processHostBlock(buffer, midi);
        EXPECT_FALSE(transport.getPositionSnapshot().playing) << "surface " << (int)surface;
    }
#else
    juce::ApplicationCommandInfo info(AppCommands::togglePlayback);
    mc.getCommandInfo(AppCommands::togglePlayback, info);
    EXPECT_NE(info.flags & juce::ApplicationCommandInfo::isDisabled, 0)
        << "togglePlayback must be inactive when the timeline integration is compiled out";
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::togglePlayback, false));
#endif
}

// ============================================================================
// 7. Delete — panel-local, per surface (already implemented; this pins the routing)
// ============================================================================

TEST_F(FocusArbitrationTest, DeletePerSurface) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    auto& editor = mc.getGraphEditor();
    auto& graph = mc.getAudioEngine().getGraph();

    // A specific node, not selectAllModules() — MainComponent's default patch already has several
    // nodes, and selectAllModules() would select (and later delete) all of them, not just this one.
    auto node = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
    ASSERT_NE(node, nullptr);
    editor.setSelectedNodes({node->nodeID});
    ASSERT_EQ(editor.getSelectionCount(), 1);
    const int nodesBefore = graph.getNumNodes();

    const juce::KeyPress deleteKey(juce::KeyPress::deleteKey);

#if SYNTH_ENABLE_TIMELINE
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    mc.getTimelinePanel().getClipSelection().setSelection({clip1});
    auto& clipLane = mc.getTimelinePanel().getClipLaneArea();

    // A) Clips-focused Delete acts ONLY on the clip selection.
    EXPECT_TRUE(clipLane.keyPressed(deleteKey));
    EXPECT_TRUE(doc.getTrack(trackA)->clips.empty());
    EXPECT_EQ(editor.getSelectionCount(), 1) << "graph selection untouched by a clips-focused Delete";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);

    // An empty clip selection falls through rather than eating the key.
    EXPECT_FALSE(clipLane.keyPressed(deleteKey));

    // A fresh clip must survive a graph-focused Delete below.
    const auto clip2 = doc.addClip(trackA, 0.0, 4.0, "C2");
    ASSERT_TRUE(clip2.isValid());
#endif

    // B) Graph-focused Delete acts ONLY on the graph selection.
    EXPECT_TRUE(editor.keyPressed(deleteKey));
    EXPECT_EQ(graph.getNumNodes(), nodesBefore - 1);
#if SYNTH_ENABLE_TIMELINE
    EXPECT_NE(doc.getClip(clip2), nullptr) << "clips untouched by a graph-focused Delete";
#endif

    // An empty graph selection falls through rather than eating the key.
    EXPECT_FALSE(editor.keyPressed(deleteKey));
}

// ============================================================================
// 8. resolveEditSurface() itself — override path + panel-visibility fallback
// ============================================================================

// A real, OS-tracked keyboard-focus grab requires a native peer (Component::addToDesktop()).
// Nothing in this test suite has ever done that (grabKeyboardFocus()/getCurrentlyFocusedComponent
// have zero prior test usages), and it is a real risk of flakiness/hangs on a headless CI runner
// with no display. Rather than being the first test to try it, this pins the two things
// resolveEditSurface() actually depends on without one:
//   (1) the test-override path every OTHER test in this file relies on as real focus's headless
//       stand-in, across all three EditSurface values;
//   (2) the panel-visibility fallback — the timeline panel starts hidden, and resolveEditSurface()
//       must resolve to Graph regardless of what (if anything) getCurrentlyFocusedComponent()
//       reports, including a best-effort grabKeyboardFocus() call on a component that was never
//       added to the desktop (which may or may not actually move JUCE's static focus pointer in
//       this environment — the assertion holds either way, because isTimelineVisible gates the
//       focus check entirely).
TEST_F(FocusArbitrationTest, SurfaceResolverRealFocus) {
    MainComponent mc(std::make_unique<FocusMockProvider>());

    ASSERT_FALSE(mc.isTimelineConfiguredVisible()) << "the panel starts hidden by default";
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph) << "no override, panel hidden -> Graph";

    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::TimelineClips);
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::PianoRoll);
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph);

    mc.setEditSurfaceOverrideForTest(std::nullopt);
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph)
        << "clearing the override falls back to the real-focus resolver, which is Graph here since "
           "the panel is hidden";

#if SYNTH_ENABLE_TIMELINE
    // Best-effort real-focus attempt: the component is never added to the desktop, so this may
    // silently no-op. Either way the panel-visibility gate must win.
    mc.getTimelinePanel().getClipLaneArea().grabKeyboardFocus();
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph)
        << "a hidden panel never owns the verbs, whatever the focused component is";
#endif
}
