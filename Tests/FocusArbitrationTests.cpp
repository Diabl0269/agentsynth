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
#include "../Source/UI/PreferencesSettingsTab.h"
#include "../Source/UI/Theme/AppLookAndFeel.h"
#include "../Source/UI/Theme/ThemeManager.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <utility>
#include <vector>

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

// The one on-disk settings file every MainComponent in this process opens (see
// synth::userSettingsOptions) — factored out of resetTimelinePanelVisibleKey so the guard below can
// reach the same file.
juce::PropertiesFile::Options userSettingsTestOptions() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth";
    opts.folderName = "Agent Synth";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    return opts;
}

// Saves the named settings keys on construction and restores them EXACTLY on destruction, including
// the case where a key did not exist at all.
//
// Needed because the commands under test persist as a side effect: setSnapValue() writes
// "timelineSnap"/"timelineSnapEnabled", and the Preferences toggle writes "naturalScrolling" — all
// into the REAL user settings file this machine's app reads. Clearing them afterwards would not be
// enough: it would silently change the developer's own preferences (and, for the snap keys, flip the
// documented CopyPasteClipsRebasedAtPlayhead local-vs-CI behaviour), so the original values go back.
//
// Both the read and the write use their own short-lived juce::ApplicationProperties, exactly as
// resetTimelinePanelVisibleKey does: a PropertiesFile saves its WHOLE in-memory property set, so a
// long-lived instance held across the test would write back a snapshot taken before the test and
// clobber every unrelated key the test happened to touch.
class PersistedKeysGuard {
public:
    explicit PersistedKeysGuard(juce::StringArray keys) {
        juce::ApplicationProperties props;
        props.setStorageParameters(userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        for (const auto& key : keys) {
            std::optional<juce::String> value;
            if (settings != nullptr && settings->containsKey(key))
                value = settings->getValue(key);
            saved_.emplace_back(key, value);
        }
    }

    ~PersistedKeysGuard() {
        juce::ApplicationProperties props;
        props.setStorageParameters(userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        if (settings == nullptr)
            return;
        for (const auto& [key, value] : saved_) {
            if (value.has_value())
                settings->setValue(key, *value);
            else
                settings->removeValue(key);
        }
        settings->saveIfNeeded();
    }

private:
    std::vector<std::pair<juce::String, std::optional<juce::String>>> saved_;
};

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
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    int requestTimeoutMs = 240000;
};

// Pins the one piece of persisted, machine-dependent state the tests below would otherwise inherit.
// TimelinePanelComponent restores snapEnabled/snap from the REAL user settings file at construction
// (restoreViewPreferences -> "timelineSnap"/"timelineSnapEnabled"), which is exactly why
// CopyPasteClipsRebasedAtPlayhead above is red on a machine whose owner turned snap off and green in
// CI. Every test added after it therefore pins the shared view state AFTER construction, and writes
// the fields DIRECTLY rather than calling setSnapEnabled() — that setter persists, which would make
// one test's arrangement leak into the next run's defaults.
void pinSnapOff(MainComponent& mc) {
    auto& view = mc.getTimelinePanel().getViewState();
    view.snap = synth::ui::TimelineViewState::Snap::Off;
    view.snapEnabled = false;
}

// True when getCommandInfo reports `cmdId` as enabled for whatever surface `mc` currently resolves
// to. juce::ApplicationCommandInfo carries "disabled" rather than "active", so every call site would
// otherwise repeat the same flag-mask double negative.
bool commandIsActive(MainComponent& mc, juce::CommandID cmdId) {
    juce::ApplicationCommandInfo info(cmdId);
    mc.getCommandInfo(cmdId, info);
    return (info.flags & juce::ApplicationCommandInfo::isDisabled) == 0;
}

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

// ============================================================================
// 1b. Graph surface — Cut is copy + the ordinary delete path, as ONE undo step
//
// The graph has no cut verb of its own: perform() composes copySelection() (clipboard only, no
// graph mutation, no undo entry) with deleteSelection() (one recordStructuralChange, the same
// transaction the Delete key uses). This pins both halves — the clipboard is filled AND a single
// undo brings every cut module back.
// ============================================================================

TEST_F(FocusArbitrationTest, CutModulesIsOneGraphUndoStep) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);

    auto& editor = mc.getGraphEditor();
    auto& graph = mc.getAudioEngine().getGraph();

    // Two specific nodes rather than selectAllModules(): the cut has to come back as ONE step, and
    // that is only meaningful with more than one module in the transaction.
    auto nodeA = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
    auto nodeB = graph.addNode(synth::AIStateMapper::createModule("Filter"));
    ASSERT_NE(nodeA, nullptr);
    ASSERT_NE(nodeB, nullptr);
    editor.setSelectedNodes({nodeA->nodeID, nodeB->nodeID});
    ASSERT_EQ(editor.getSelectionCount(), 2);

    const int nodesBefore = graph.getNumNodes();
    ASSERT_TRUE(commandIsActive(mc, AppCommands::cutSelection));

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_EQ(graph.getNumNodes(), nodesBefore - 2);
    EXPECT_TRUE(editor.canPaste()) << "the copy half ran before the delete half";
    EXPECT_EQ(editor.getSelectionCount(), 0);

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "one Cmd+Z brings the whole cut back";
    EXPECT_TRUE(editor.canPaste()) << "the clipboard survives undoing the cut";
}

// ============================================================================
// 1c. Graph surface — Cut needs a selection; Repeat is unsupported here outright
// ============================================================================

TEST_F(FocusArbitrationTest, CutInactiveAndRepeatUnsupportedOnGraph) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);

    auto& editor = mc.getGraphEditor();
    auto& cm = mc.getCommandManager();
    ASSERT_EQ(editor.getSelectionCount(), 0) << "a fresh canvas starts with nothing selected";

    EXPECT_FALSE(commandIsActive(mc, AppCommands::cutSelection));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::cutSelection, false));

    // Repeat stays inactive on the graph even WITH a selection — it is a time-axis verb and the
    // canvas has no time axis (Duplicate is the graph's answer). performRepeatSelection agrees, so
    // a direct/scripted caller gets the same "no" the greyed-out menu row does.
    editor.selectAllModules();
    ASSERT_GT(editor.getSelectionCount(), 0);
    EXPECT_TRUE(commandIsActive(mc, AppCommands::cutSelection)) << "Cut follows the selection";
    EXPECT_FALSE(commandIsActive(mc, AppCommands::repeatSelection));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::repeatSelection, false));
    EXPECT_FALSE(mc.performRepeatSelection(3));
}

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
// 5. PianoRoll surface — C/V/D with nothing to act on
//
// The v1 gap (an unconditional setActive(false) on this surface) is GONE: the three verbs now route
// into the roll's own note clipboard, so "inactive" has to come from the roll's real state — no
// selection, and a clipboard with nowhere to go — rather than from the command wiring refusing to
// look. Same arrangement the old gap test used, so a regression that re-hardcodes setActive(false)
// still passes here and fails the two tests below it.
// ============================================================================

TEST_F(FocusArbitrationTest, PianoRollSurfaceCVDInactiveWithNothingSelected) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});
    mc.getTimelinePanel().getClipSelection().setSelection({clip1});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);

    // The roll was never opened and nothing is selected in it: Copy/Duplicate have no notes, and
    // Paste has neither a clipboard nor an open clip (canPasteNotes needs BOTH).
    auto& roll = mc.getTimelinePanel().getPianoRoll();
    ASSERT_FALSE(roll.isOpen());
    ASSERT_FALSE(roll.hasNoteSelection());
    for (auto cmdId : {AppCommands::copySelection, AppCommands::pasteSelection, AppCommands::duplicateSelection,
                       AppCommands::cutSelection}) {
        EXPECT_FALSE(commandIsActive(mc, cmdId))
            << "command " << cmdId << " must be inactive on an empty piano-roll surface";
    }

    auto& cm = mc.getCommandManager();
    const auto revisionBefore = doc.getRevision();
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::copySelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::pasteSelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::duplicateSelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_EQ(doc.getRevision(), revisionBefore) << "an inactive command must never touch the doc";
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 1u);

    // Opening the roll alone does NOT make Paste available — the note clipboard is still empty.
    mc.getTimelinePanel().openPianoRoll(clip1);
    ASSERT_TRUE(roll.isOpen());
    EXPECT_FALSE(commandIsActive(mc, AppCommands::pasteSelection))
        << "an open clip with an empty note clipboard still has nothing to paste";
}

// ============================================================================
// 5b. PianoRoll surface — the verbs are ACTIVE once notes are selected
// ============================================================================

TEST_F(FocusArbitrationTest, PianoRollCopyDuplicateCutActiveWithNoteSelection) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 8.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});
    doc.addNote(clip1, synth::MidiNote{{}, 1.0, 1.0, 64, 100, 1});

    // openPianoRoll clears the note selection, so select AFTER opening.
    mc.getTimelinePanel().openPianoRoll(clip1);
    auto& roll = mc.getTimelinePanel().getPianoRoll();
    ASSERT_TRUE(roll.isOpen());
    ASSERT_TRUE(roll.selectAllNotes());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);

    EXPECT_TRUE(commandIsActive(mc, AppCommands::copySelection));
    EXPECT_TRUE(commandIsActive(mc, AppCommands::duplicateSelection));
    EXPECT_TRUE(commandIsActive(mc, AppCommands::cutSelection));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::pasteSelection)) << "nothing copied yet";

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::copySelection, false));
    EXPECT_TRUE(roll.canPasteNotes());
    EXPECT_TRUE(commandIsActive(mc, AppCommands::pasteSelection)) << "a filled note clipboard enables Paste";

    // Duplicate lands the block one span (2 beats) later: 2.0 and 3.0.
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::duplicateSelection, false));
    ASSERT_EQ(doc.getClip(clip1)->notes.size(), 4u);

    // The clipboard survives losing the selection; Copy/Duplicate/Cut do not.
    roll.getSelectionForTest().clear();
    EXPECT_FALSE(commandIsActive(mc, AppCommands::copySelection));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::duplicateSelection));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::cutSelection));
    EXPECT_TRUE(commandIsActive(mc, AppCommands::pasteSelection));
}

// ============================================================================
// 5c. PianoRoll surface — Paste anchors on the LIVE transport beat
//
// The roll has no transport of its own: pasteNotesAtPlayhead anchors on the last beat something
// PUSHED into it via setPlayheadBeat, and a stopped transport never pushes one. This pins the
// priming step in perform()'s PianoRoll paste arm — without it the anchor would be a stale 0.0,
// which (being outside the edited clip) would collapse to the clip's start.
// ============================================================================

TEST_F(FocusArbitrationTest, PianoRollPasteAnchorsOnPrimedPlayhead) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);

    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    // Deliberately NOT at beat 0: an un-primed paste would anchor at a stale 0.0, which is outside
    // this clip and collapses to its start — a result this test can tell apart from the real one.
    const auto clip1 = doc.addClip(trackA, 4.0, 8.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});
    doc.addNote(clip1, synth::MidiNote{{}, 1.0, 1.0, 64, 100, 1});

    mc.getTimelinePanel().openPianoRoll(clip1);
    auto& roll = mc.getTimelinePanel().getPianoRoll();
    ASSERT_TRUE(roll.isOpen());
    ASSERT_TRUE(roll.selectAllNotes());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);

    // Locate, then drain one host block so the position snapshot reflects it (file-header rule).
    // The transport is STOPPED throughout — which is precisely the case that needs priming.
    auto& transport = engine.getTransport();
    transport.locateBeat(6.0);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    engine.processHostBlock(buffer, midi);
    ASSERT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 6.0);
    ASSERT_FALSE(transport.getPositionSnapshot().playing);

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::copySelection, false));
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::pasteSelection, false));

    // Absolute beat 6.0 inside a clip starting at 4.0 -> clip-relative anchor 2.0 (snap pinned off).
    const auto pasted = roll.getSelectionForTest().getSelected();
    ASSERT_EQ(pasted.size(), 2u) << "the pasted block becomes the selection";
    std::vector<double> starts;
    for (auto id : pasted)
        starts.push_back(doc.getNote(id)->startBeat);
    std::sort(starts.begin(), starts.end());
    EXPECT_DOUBLE_EQ(starts[0], 2.0) << "anchored on the LIVE transport beat, not a stale 0.0";
    EXPECT_DOUBLE_EQ(starts[1], 3.0) << "the block keeps its internal shape";
    EXPECT_EQ(doc.getClip(clip1)->notes.size(), 4u);
}

// ============================================================================
// 9. Cut — per surface, one undo step each
// ============================================================================

TEST_F(FocusArbitrationTest, CutClipsIsOneUndoStep) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    const auto clip2 = doc.addClip(trackA, 8.0, 4.0, "C2");
    ASSERT_TRUE(clip1.isValid());
    ASSERT_TRUE(clip2.isValid());

    auto& panel = mc.getTimelinePanel();
    panel.getClipSelection().setSelection({clip1, clip2});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    ASSERT_TRUE(commandIsActive(mc, AppCommands::cutSelection));

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_TRUE(doc.getTrack(trackA)->clips.empty());
    EXPECT_TRUE(panel.canPasteClips()) << "a cut always leaves something pasteable";

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 2u) << "one Cmd+Z brings the whole cut back";
    EXPECT_TRUE(panel.canPasteClips()) << "the clipboard survives undoing the cut";
}

TEST_F(FocusArbitrationTest, CutNotesIsOneUndoStep) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 8.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});
    doc.addNote(clip1, synth::MidiNote{{}, 2.0, 1.0, 64, 100, 1});

    mc.getTimelinePanel().openPianoRoll(clip1);
    auto& roll = mc.getTimelinePanel().getPianoRoll();
    ASSERT_TRUE(roll.selectAllNotes());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);
    ASSERT_TRUE(commandIsActive(mc, AppCommands::cutSelection));

    auto& cm = mc.getCommandManager();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_TRUE(doc.getClip(clip1)->notes.empty());
    EXPECT_TRUE(roll.canPasteNotes()) << "a cut always leaves something pasteable";
    EXPECT_FALSE(roll.hasNoteSelection());

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(doc.getClip(clip1)->notes.size(), 2u) << "one Cmd+Z brings the whole cut back";
}

// ============================================================================
// 10. Select All — per surface, the other surfaces untouched
// ============================================================================

TEST_F(FocusArbitrationTest, SelectAllRoutesPerSurface) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto trackB = doc.addTrack(synth::TrackKind::Midi, "B");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    const auto clip2 = doc.addClip(trackB, 8.0, 4.0, "C2");
    ASSERT_TRUE(clip1.isValid());
    ASSERT_TRUE(clip2.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});
    doc.addNote(clip1, synth::MidiNote{{}, 1.0, 1.0, 64, 100, 1});

    auto& panel = mc.getTimelinePanel();
    auto& roll = panel.getPianoRoll();
    auto& editor = mc.getGraphEditor();
    auto& cm = mc.getCommandManager();

    // A) Graph.
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::selectAllModules, false));
    const int graphSelected = editor.getSelectionCount();
    EXPECT_GT(graphSelected, 0);
    EXPECT_TRUE(panel.getClipSelection().isEmpty()) << "clips untouched by a graph Select All";
    EXPECT_FALSE(roll.hasNoteSelection()) << "notes untouched by a graph Select All";

    // B) TimelineClips.
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::selectAllModules, false));
    EXPECT_EQ(panel.getClipSelection().size(), 2) << "every clip on every track";
    EXPECT_EQ(editor.getSelectionCount(), graphSelected) << "graph selection untouched";
    EXPECT_FALSE(roll.hasNoteSelection()) << "notes untouched by a clips Select All";

    // C) PianoRoll — openPianoRoll clears the note selection, so this really is Select All's doing.
    panel.openPianoRoll(clip1);
    ASSERT_TRUE(roll.isOpen());
    ASSERT_FALSE(roll.hasNoteSelection());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::selectAllModules, false));
    EXPECT_EQ(roll.getSelectionForTest().getSelected().size(), 2u) << "every note in the open clip";
    EXPECT_EQ(panel.getClipSelection().size(), 2) << "clip selection untouched by a roll Select All";
    EXPECT_EQ(editor.getSelectionCount(), graphSelected) << "graph selection untouched";
}

// ============================================================================
// 11. Repeat — performRepeatSelection() skips the dialog, so tests never go modal
// ============================================================================

TEST_F(FocusArbitrationTest, RepeatSelectionTilesClips) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    ASSERT_TRUE(clip1.isValid());

    auto& panel = mc.getTimelinePanel();
    panel.getClipSelection().setSelection({clip1});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    EXPECT_TRUE(commandIsActive(mc, AppCommands::repeatSelection));

    ASSERT_TRUE(mc.performRepeatSelection(3));
    ASSERT_EQ(doc.getTrack(trackA)->clips.size(), 4u) << "the source plus three copies";

    // Block length is the selection's span (4 beats), so the copies tile at 4, 8, 12.
    const auto created = panel.getClipSelection().getSelected();
    ASSERT_EQ(created.size(), 3u) << "the repeat's own copies end up selected";
    std::vector<double> starts;
    for (auto id : created)
        starts.push_back(doc.getClip(id)->startBeat);
    std::sort(starts.begin(), starts.end());
    EXPECT_DOUBLE_EQ(starts[0], 4.0);
    EXPECT_DOUBLE_EQ(starts[1], 8.0);
    EXPECT_DOUBLE_EQ(starts[2], 12.0);

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 1u) << "one undo step for the whole repeat";
}

TEST_F(FocusArbitrationTest, RepeatSelectionClampsToTheMaxRepeatCount) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    ASSERT_TRUE(clip1.isValid());

    auto& panel = mc.getTimelinePanel();
    panel.getClipSelection().setSelection({clip1});
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);

    ASSERT_TRUE(mc.performRepeatSelection(1000));
    ASSERT_EQ(doc.getTrack(trackA)->clips.size(), 65u) << "the source plus a clamped 64 copies";

    const auto created = panel.getClipSelection().getSelected();
    EXPECT_EQ(created.size(), 64u) << "the count is clamped, not the requested 1000";

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(doc.getTrack(trackA)->clips.size(), 1u) << "one undo step for the whole (clamped) repeat";
    EXPECT_FALSE(um.canUndo());
}

TEST_F(FocusArbitrationTest, RepeatSelectionRepeatsNotes) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 16.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});

    mc.getTimelinePanel().openPianoRoll(clip1);
    auto& roll = mc.getTimelinePanel().getPianoRoll();
    ASSERT_TRUE(roll.selectAllNotes());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);
    EXPECT_TRUE(commandIsActive(mc, AppCommands::repeatSelection));

    ASSERT_TRUE(mc.performRepeatSelection(3));
    ASSERT_EQ(doc.getClip(clip1)->notes.size(), 4u) << "the source plus three copies";

    const auto created = roll.getSelectionForTest().getSelected();
    ASSERT_EQ(created.size(), 3u);
    std::vector<double> starts;
    for (auto id : created)
        starts.push_back(doc.getNote(id)->startBeat);
    std::sort(starts.begin(), starts.end());
    EXPECT_DOUBLE_EQ(starts[0], 1.0) << "one span (1 beat) apart";
    EXPECT_DOUBLE_EQ(starts[1], 2.0);
    EXPECT_DOUBLE_EQ(starts[2], 3.0);

    auto& um = mc.getUndoManager();
    ASSERT_TRUE(um.canUndo());
    um.undo();
    EXPECT_EQ(doc.getClip(clip1)->notes.size(), 1u) << "one undo step for the whole repeat";
}

// ============================================================================
// 12. Cut/Repeat inactive states on the timeline surfaces
// ============================================================================

TEST_F(FocusArbitrationTest, CutAndRepeatInactiveWithEmptyTimelineSelections) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    pinSnapOff(mc);
    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    const auto clip1 = doc.addClip(trackA, 0.0, 4.0, "C1");
    ASSERT_TRUE(clip1.isValid());
    doc.addNote(clip1, synth::MidiNote{{}, 0.0, 1.0, 60, 100, 1});

    auto& panel = mc.getTimelinePanel();
    ASSERT_TRUE(panel.getClipSelection().isEmpty());

    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::TimelineClips);
    EXPECT_FALSE(commandIsActive(mc, AppCommands::cutSelection));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::repeatSelection));

    // An OPEN roll with no note selection is still nothing to cut or repeat.
    panel.openPianoRoll(clip1);
    ASSERT_TRUE(panel.getPianoRoll().isOpen());
    ASSERT_FALSE(panel.getPianoRoll().hasNoteSelection());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);
    EXPECT_FALSE(commandIsActive(mc, AppCommands::cutSelection));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::repeatSelection));

    auto& cm = mc.getCommandManager();
    const auto revisionBefore = doc.getRevision();
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_FALSE(cm.invokeDirectly(AppCommands::repeatSelection, false));
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

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

    // B) Graph-focused Delete acts ONLY on the graph selection.
    EXPECT_TRUE(editor.keyPressed(deleteKey));
    EXPECT_EQ(graph.getNumNodes(), nodesBefore - 1);
    EXPECT_NE(doc.getClip(clip2), nullptr) << "clips untouched by a graph-focused Delete";

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

    // Best-effort real-focus attempt: the component is never added to the desktop, so this may
    // silently no-op. Either way the panel-visibility gate must win.
    mc.getTimelinePanel().getClipLaneArea().grabKeyboardFocus();
    EXPECT_EQ(mc.resolveEditSurface(), MainComponent::EditSurface::Graph)
        << "a hidden panel never owns the verbs, whatever the focused component is";
}

// ============================================================================
// 9. Surface actions must fall THROUGH the global handler
//
// The shortcut table now holds bare arrows, Q/L/P and the tool digits — rebindable, but resolved by
// the component that owns the key rather than dispatched as commands. MainComponent::keyPressed is
// the last stop for every key, so it has to ignore them: a bare Left that got this far means no
// surface claimed it, and both possible mistakes are silent. Swallowing it (returning true) breaks
// whatever the parent chain would have done next; trying to dispatch it looks up a command that does
// not exist. This is the test that would have caught either.
// ============================================================================

TEST_F(FocusArbitrationTest, BareArrowKeyFallsThroughTheGlobalHandlerUntouched) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);

    auto& editor = mc.getGraphEditor();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& shortcuts = mc.getShortcutManager();

    const juce::KeyPress bareLeft(juce::KeyPress::leftKey, juce::ModifierKeys::noModifiers, 0);

    // Precondition, and the whole point: the key IS bound — to a surface action with no command. A
    // key that were simply unbound would make the assertion below pass for the wrong reason.
    ASSERT_TRUE(shortcuts.getActionsForKeyPress(bareLeft).contains("pianoRollNudgeLeft"));
    ASSERT_EQ(AppCommands::getCommandForAction("pianoRollNudgeLeft"), AppCommands::kNoCommand);

    auto node = graph.addNode(synth::AIStateMapper::createModule("Oscillator"));
    ASSERT_NE(node, nullptr);
    editor.setSelectedNodes({node->nodeID});
    const int nodesBefore = graph.getNumNodes();
    const int selectionBefore = editor.getSelectionCount();

    EXPECT_FALSE(mc.keyPressed(bareLeft)) << "an unclaimed surface key must fall through, not be swallowed";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "nothing was invoked";
    EXPECT_EQ(editor.getSelectionCount(), selectionBefore) << "and nothing was cleared either";

    // Same for the other five piano-roll arrows and for the bare tool digits, so a future rename
    // cannot leave one of them dispatching.
    for (const auto& kp : {juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0),
                           juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::noModifiers, 0),
                           juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::noModifiers, 0),
                           juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0),
                           juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0),
                           juce::KeyPress('3', juce::ModifierKeys::noModifiers, 0),
                           juce::KeyPress('q', juce::ModifierKeys::noModifiers, 0),
                           juce::KeyPress('p', juce::ModifierKeys::noModifiers, 0)})
        EXPECT_FALSE(mc.keyPressed(kp)) << ShortcutManager::keyPressToDisplayString(kp).toStdString();
}

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
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());

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
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());

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
    ASSERT_FALSE(mc.isTimelineConfiguredVisible()) << "the panel starts hidden by default";

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
// 14. "Scroll up zooms in" — the same settings-file path, the other wheel flag
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

// ============================================================================
// 15. REAL-KEYBOARD dispatch: the shifted-symbol key codes the macOS peer delivers
//
// The one test in this file that goes through MainComponent::keyPressed with the key code a REAL
// keystroke carries rather than the one the binding stores. juce_NSViewComponentPeer_mac.mm's
// getKeyCodeFromEvent() builds the code from [ev charactersIgnoringModifiers][0] and — as its own
// comment concedes — only compensates for Shift by upper-casing LETTERS, so Ctrl+Shift+1 arrives as
// KeyPress('!', ctrl|shift). Every other grid test invokes the command directly or constructs the
// digit, which is exactly why this whole block shipped dead: the fix is
// ShortcutManager::keyPressMatches, and this is the end-to-end proof that it reaches perform().
// ============================================================================

// MainComponent::keyPressed dispatches its command ASYNCHRONOUSLY (invokeDirectly(cmd, true) — the
// real key handler must not run a command re-entrantly mid-key-event), so a test that drives
// keyPressed directly has to pump the message loop before asserting the command's effect.
static bool pressAndPump(MainComponent& mc, const juce::KeyPress& key) {
    const bool consumed = mc.keyPressed(key);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    return consumed;
}

TEST_F(FocusArbitrationTest, ShiftedSymbolKeyCodesFromTheRealKeyboardReachTheGridCommands) {
    PersistedKeysGuard guard({"timelineSnap", "timelineSnapEnabled"});

    using Snap = synth::ui::TimelineViewState::Snap;
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1200, 800);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isTimelineConfiguredVisible()) << "precondition: the grid commands are panel-gated";

    auto& view = mc.getTimelinePanel().getViewState();
    view.setSnap(Snap::Quarter);

    const int ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;

    // THE reported bug: the user pressed Ctrl+Shift+1 and nothing happened.
    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('!', juce::ModifierKeys(ctrlShift), '!')));
    EXPECT_EQ(view.snap, Snap::Whole);

    // The three new divisions, by the shifted glyphs 6/7/8 actually produce.
    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('^', juce::ModifierKeys(ctrlShift), '^')));
    EXPECT_EQ(view.snap, Snap::ThirtySecond);
    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('&', juce::ModifierKeys(ctrlShift), '&')));
    EXPECT_EQ(view.snap, Snap::SixtyFourth);
    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('*', juce::ModifierKeys(ctrlShift), '*')));
    EXPECT_EQ(view.snap, Snap::HundredTwentyEighth);

    // The digit form still dispatches too — a layout (or a platform) that DOES ignore Shift properly
    // must keep working, since the normalization is an addition to exact matching, not a replacement.
    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('5', juce::ModifierKeys(ctrlShift), '5')));
    EXPECT_EQ(view.snap, Snap::Sixteenth);

    // And the bare tool digits are NOT reachable this way: '&' with no modifiers matches nothing (the
    // normalization needs Shift on both sides), so keyPressed finds no command and reports unhandled.
    EXPECT_FALSE(pressAndPump(mc, juce::KeyPress('&', juce::ModifierKeys::noModifiers, '&')));
    EXPECT_EQ(view.snap, Snap::Sixteenth) << "and nothing moved the grid either";
}

// The vertical zoom pair was dead in the app for exactly the same reason: Cmd+Shift+'=' arrives as
// '+'. Routed per focused surface, so this drives the piano roll's own mapping.
TEST_F(FocusArbitrationTest, ShiftedPunctuationReachesTheVerticalZoomCommands) {
    MainComponent mc(std::make_unique<FocusMockProvider>());
    mc.setSize(1200, 800);
    mc.simulateToggleTimelineClick();
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());
    mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::PianoRoll);

    auto& roll = mc.getTimelinePanel().getPianoRoll();
    const double semisBefore = roll.getPixelsPerSemitone();
    const int cmdShift = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('+', juce::ModifierKeys(cmdShift), '+')));
    EXPECT_GT(roll.getPixelsPerSemitone(), semisBefore);
    const double afterIn = roll.getPixelsPerSemitone();

    EXPECT_TRUE(pressAndPump(mc, juce::KeyPress('_', juce::ModifierKeys(cmdShift), '_')));
    EXPECT_LT(roll.getPixelsPerSemitone(), afterIn) << "'_' is Cmd+Shift+'-', the zoom-OUT half of the pair";
}
