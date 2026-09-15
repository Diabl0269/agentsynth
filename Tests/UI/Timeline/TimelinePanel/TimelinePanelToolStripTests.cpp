// TimelinePanelToolStripTests.cpp
//
// The edit-tool strip + the clip clipboard/arrangement verbs the app's Cut/Copy/Paste/
// Duplicate/Select All/Repeat commands delegate to. Panel level, ungated. ToolPanelFixture is
// local to this file.

#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "MainComponent/MainComponent.h"
#include "ProjectBundle.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "TimelinePanelTestFixture.h"
#include "Transport/TransportService.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TrackColour.h"
#include "UserSettings.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

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

// The panel's three letter keys go through the same resolution. J is the snap toggle
// ("timelineSnapToggle"), Cubase's snap key — Q belongs to the roll's quantise.
TEST(TimelineToolStripTest, PanelLetterKeysResolveThroughTheShortcutManager) {
    ToolPanelFixture f;
    ShortcutManager shortcuts;
    f.panel.setShortcutManager(&shortcuts);

    auto& view = f.panel.getViewState();
    const bool snapBefore = view.snapEnabled;
    ASSERT_TRUE(f.panel.keyPressed(juce::KeyPress('j')));
    EXPECT_EQ(view.snapEnabled, !snapBefore);

    shortcuts.setBinding("timelineSnapToggle", juce::KeyPress('y', juce::ModifierKeys::noModifiers, 0));
    EXPECT_FALSE(f.panel.keyPressed(juce::KeyPress('j'))) << "the old key falls through";
    EXPECT_EQ(view.snapEnabled, !snapBefore) << "and did not toggle again";
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('y')));
    EXPECT_EQ(view.snapEnabled, snapBefore);

    // A MODIFIED J is a different chord and must not reach the panel's snap toggle (the
    // pre-shortcut code matched its letter on the key code alone, ignoring modifiers entirely).
    f.panel.setShortcutManager(nullptr);
    const bool snapNow = view.snapEnabled;
    EXPECT_FALSE(f.panel.keyPressed(juce::KeyPress('j', juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_EQ(view.snapEnabled, snapNow);
}

// ============================================================================
// Dynamic shortcut-hint tooltips (see synth::shortcutHintFor / TimelinePanelComponent::
// refreshShortcutTooltips) — the tool strip, the snap toggle and the follow-playhead toggle all
// embed the CURRENT binding rather than a hardcoded key name, and rebuild it on every
// bindings-changed notification.
// ============================================================================

// Regression guard for the reported gap: every OTHER button in this strip already named its own
// key ("Select (1)", "Snap to grid on/off (Q)"); Follow playhead used to be the bare
// "Follow playhead" with no hint at all.
TEST(TimelineToolStripTest, FollowPlayheadTooltipNamesItsKeyLikeItsSiblingsDo) {
    ToolPanelFixture f;
    EXPECT_TRUE(f.panel.getFollowPlayheadButtonForTest().getTooltip().contains("(f)"))
        << "no manager installed -- the hardcoded default 'f', lowercase (see shortcutHintFor)";
}

TEST(TimelineToolStripTest, FollowPlayheadTooltipTracksALiveRebindAndDropsTheOldKey) {
    ToolPanelFixture f;
    ShortcutManager shortcuts;
    f.panel.setShortcutManager(&shortcuts);
    auto& followButton = f.panel.getFollowPlayheadButtonForTest();
    ASSERT_TRUE(followButton.getTooltip().contains("(f)"));

    shortcuts.setBinding("timelineFollowPlayheadToggle", juce::KeyPress('g', juce::ModifierKeys::noModifiers, 0));
    shortcuts.saveToProperties(); // broadcasts the change even with no ApplicationProperties wired

    const auto tooltip = followButton.getTooltip();
    EXPECT_TRUE(tooltip.contains("(g)")) << "the tooltip now names the current key";
    EXPECT_FALSE(tooltip.contains("(f)")) << "and not the stale one";

    // Detach before `shortcuts` (declared after `f`, so destroyed first) goes out of scope — an
    // ASAN run caught the heap-use-after-free this leaves otherwise (unrelated to FRO95, but hit
    // while chasing it): TimelinePanelComponent::~TimelinePanelComponent() removes itself as a
    // change listener from whatever ShortcutManager is still installed, which is a dangling
    // pointer once `shortcuts` is gone. Every other test in this file that installs one already
    // does this (see ClearingTheSharedShortcutManagerRestoresTheHardcodedTooltipDefaults below).
    f.panel.setShortcutManager(nullptr);
}

TEST(TimelineToolStripTest, ToolStripAndSnapToggleTooltipsTrackTheirLiveBindings) {
    ToolPanelFixture f;
    ShortcutManager shortcuts;
    f.panel.setShortcutManager(&shortcuts);

    auto* selectButton = f.panel.getToolButton(synth::ui::EditTool::Select);
    ASSERT_NE(selectButton, nullptr);
    EXPECT_TRUE(selectButton->getTooltip().contains("(1)"));
    EXPECT_TRUE(f.panel.getSnapToggleButton().getTooltip().contains("(j)")) << "snap is J now, not Q";

    shortcuts.setBinding("timelineToolSelect", juce::KeyPress('k', juce::ModifierKeys::noModifiers, 0));
    shortcuts.setBinding("timelineSnapToggle", juce::KeyPress('y', juce::ModifierKeys::noModifiers, 0));
    shortcuts.saveToProperties();

    EXPECT_TRUE(selectButton->getTooltip().contains("(k)"));
    EXPECT_FALSE(selectButton->getTooltip().contains("(1)"));
    EXPECT_TRUE(f.panel.getSnapToggleButton().getTooltip().contains("(y)"));

    // Detach before `shortcuts` goes out of scope — see the comment in the previous test.
    f.panel.setShortcutManager(nullptr);
}

TEST(TimelineToolStripTest, ClearingTheSharedShortcutManagerRestoresTheHardcodedTooltipDefaults) {
    ToolPanelFixture f;
    ShortcutManager shortcuts;
    f.panel.setShortcutManager(&shortcuts);
    shortcuts.setBinding("timelineFollowPlayheadToggle", juce::KeyPress('g', juce::ModifierKeys::noModifiers, 0));
    shortcuts.saveToProperties();
    ASSERT_TRUE(f.panel.getFollowPlayheadButtonForTest().getTooltip().contains("(g)"));

    f.panel.setShortcutManager(nullptr);
    EXPECT_TRUE(f.panel.getFollowPlayheadButtonForTest().getTooltip().contains("(f)"));
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

    // Detach before `shortcuts` goes out of scope — see the comment on
    // FollowPlayheadTooltipTracksALiveRebindAndDropsTheOldKey above.
    lane.setShortcutManager(nullptr);
}

TEST(TimelineToolStripTest, ButtonsMirrorTheActiveToolAndCarryTheirShortcutInTheTooltip) {
    ToolPanelFixture f;

    for (auto tool : synth::ui::kAllEditTools)
        ASSERT_NE(f.panel.getToolButton(tool), nullptr) << "every tool has a button, headless included";

    EXPECT_EQ(f.panel.getToolButton(synth::ui::EditTool::Split)->getTooltip(), "Split  (3)");
    EXPECT_EQ(f.panel.getToolButton(synth::ui::EditTool::Draw)->getTooltip(), "Draw  (8)");

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
