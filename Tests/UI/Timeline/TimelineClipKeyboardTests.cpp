// TimelineClipKeyboardTests.cpp
//
// Clip keyboard mode, driven through the real key paths: a focused track header's Right enters
// that track's clips; in clip mode TimelineClipLaneArea::keyPressed steps between clips, opens the
// clip, moves it one grid step and hands focus back on Escape. Real keyboard focus needs a native
// peer (see TimelineTrackFocusTests.cpp), so the tests assert the model the focus calls mirror:
// the lane's keyboard clip, the selection, the focused track index and the open piano roll.

#include "AppUndoManager.h"
#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/TransportService.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ClipId;
using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;
using synth::ui::TrackHeaderHost;

namespace {

juce::KeyPress key(int code, int mods = juce::ModifierKeys::noModifiers) {
    return juce::KeyPress(code, juce::ModifierKeys(mods), 0);
}
juce::KeyPress rightKey() { return key(juce::KeyPress::rightKey); }
juce::KeyPress leftKey() { return key(juce::KeyPress::leftKey); }
juce::KeyPress upKey() { return key(juce::KeyPress::upKey); }
juce::KeyPress downKey() { return key(juce::KeyPress::downKey); }
juce::KeyPress returnKey() { return key(juce::KeyPress::returnKey); }
juce::KeyPress escapeKey() { return key(juce::KeyPress::escapeKey); }

struct StubHost : TrackHeaderHost {
    std::vector<BindingOption> getAvailableTrackInNodes(TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
};

// Three tracks: Bass (clips at 0, 8, 16), Empty (no clips), Lead (clips at 2, 9, 30).
struct ClipKeyboardFixture {
    TimelineDoc doc;
    StubHost host;
    AppUndoManager undo;
    synth::TransportService transport;
    synth::ui::TimelinePanelComponent panel;
    TrackId bass, empty, lead;
    ClipId bass0, bass8, bass16, lead2, lead9, lead30;

    ClipKeyboardFixture() {
        panel.setSize(1200, 400);
        panel.setTrackHeaderHost(&host);
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        panel.setTransport(&transport);

        bass = doc.addTrack(TrackKind::Midi, "Bass");
        empty = doc.addTrack(TrackKind::Midi, "Empty");
        lead = doc.addTrack(TrackKind::Midi, "Lead");
        bass0 = doc.addClip(bass, 0.0, 4.0, "Bassline");
        bass8 = doc.addClip(bass, 8.0, 4.0, "Bass B");
        bass16 = doc.addClip(bass, 16.0, 4.0, "Bass C");
        lead2 = doc.addClip(lead, 2.0, 2.0, "Lead A");
        lead9 = doc.addClip(lead, 9.0, 2.0, "Lead B");
        lead30 = doc.addClip(lead, 30.0, 2.0, "Lead C");
    }

    synth::ui::TimelineClipLaneArea& lane() { return panel.getClipLaneArea(); }
    synth::ui::TimelineTrackHeaderComponent& header(int index) { return *panel.getTrackHeaderAt(index); }
    bool press(const juce::KeyPress& k) { return lane().keyPressed(k); }
    void enter(int trackIndex = 0) { ASSERT_TRUE(header(trackIndex).keyPressed(rightKey())); }
    void playheadAt(double beat) {
        ASSERT_TRUE(transport.locateBeat(beat));
        transport.tick(512);
    }
};

} // namespace

TEST(TimelineClipKeyboardTest, RightOnAHeaderEntersTheFirstClipWhenThePlayheadIsAtTheStart) {
    ClipKeyboardFixture f;
    EXPECT_TRUE(f.header(0).keyPressed(rightKey()));
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass0);
    EXPECT_EQ(f.panel.getClipSelection().getSelected(), std::vector<ClipId>{f.bass0})
        << "the keyboard clip is the single selected clip";
}

TEST(TimelineClipKeyboardTest, RightEntersTheFirstClipStartingAtOrAfterThePlayhead) {
    ClipKeyboardFixture f;
    f.playheadAt(5.0);
    f.enter(0);
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass8);

    f.playheadAt(9.0);
    f.enter(2);
    EXPECT_EQ(f.lane().getKeyboardClip(), f.lead9) << "a clip starting exactly on the playhead counts";
}

TEST(TimelineClipKeyboardTest, RightEntersTheTracksFirstClipWhenThePlayheadIsPastEveryClip) {
    ClipKeyboardFixture f;
    f.playheadAt(100.0);
    f.enter(0);
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass0);
}

TEST(TimelineClipKeyboardTest, RightOnATrackWithNoClipsDoesNothing) {
    ClipKeyboardFixture f;
    EXPECT_FALSE(f.header(1).keyPressed(rightKey()));
    EXPECT_FALSE(f.lane().getKeyboardClip().isValid());
    EXPECT_TRUE(f.panel.getClipSelection().isEmpty());
}

TEST(TimelineClipKeyboardTest, LeftAndRightStepAlongTheSameTrackAndStopAtTheEnds) {
    ClipKeyboardFixture f;
    f.enter(0);
    EXPECT_TRUE(f.press(rightKey()));
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass8);
    EXPECT_TRUE(f.press(rightKey()));
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass16);
    EXPECT_TRUE(f.press(rightKey())) << "the last clip consumes the key but stays put";
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass16);

    EXPECT_TRUE(f.press(leftKey()));
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass8);
    EXPECT_TRUE(f.press(leftKey()));
    EXPECT_TRUE(f.press(leftKey()));
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass0);
    EXPECT_EQ(f.panel.getClipSelection().size(), 1) << "stepping replaces the selection";
}

TEST(TimelineClipKeyboardTest, UpAndDownPickTheNearestClipInTimeAndSkipEmptyTracks) {
    ClipKeyboardFixture f;
    f.enter(0);
    f.press(rightKey()); // Bass clip at beat 8

    EXPECT_TRUE(f.press(downKey()));
    EXPECT_EQ(f.lane().getKeyboardClip(), f.lead9) << "the Empty track is skipped; 9 is nearer to 8 than 2 is";
    EXPECT_TRUE(f.press(upKey()));
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass8) << "9 is nearest to Bass's clip at 8";

    EXPECT_TRUE(f.press(upKey())) << "no track above: consumed, nothing changes";
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass8);
    f.press(downKey());
    EXPECT_TRUE(f.press(downKey()));
    EXPECT_EQ(f.lane().getKeyboardClip(), f.lead9) << "no track below: consumed, nothing changes";
}

TEST(TimelineClipKeyboardTest, SteppingToAnotherTrackMovesTheFocusedTrack) {
    ClipKeyboardFixture f;
    f.enter(0);
    f.press(downKey());
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 2);
}

TEST(TimelineClipKeyboardTest, EnterOpensTheKeyboardClipInThePianoRoll) {
    ClipKeyboardFixture f;
    f.enter(2);
    f.press(rightKey()); // Lead clip at 9
    ASSERT_FALSE(f.panel.getPianoRoll().isOpen());

    EXPECT_TRUE(f.press(returnKey()));
    EXPECT_TRUE(f.panel.getPianoRoll().isOpen());
    EXPECT_EQ(f.panel.getPianoRoll().getClipId(), f.lead9);
}

TEST(TimelineClipKeyboardTest, AltRightMovesTheClipOneGridStepAndUndoRestoresIt) {
    ClipKeyboardFixture f;
    f.panel.getViewState().snap = synth::ui::TimelineViewState::Snap::Quarter;
    f.enter(0);
    f.press(rightKey()); // Bass clip at 8

    EXPECT_TRUE(f.press(key(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier)));
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.bass8)->startBeat, 9.0);
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass8) << "the moved clip stays the keyboard clip";

    EXPECT_TRUE(f.press(key(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier)));
    EXPECT_TRUE(f.press(key(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier)));
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.bass8)->startBeat, 7.0);

    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.bass8)->startBeat, 8.0) << "each move is one undo step";
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.bass8)->startBeat, 9.0);
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.bass8)->startBeat, 8.0);
}

TEST(TimelineClipKeyboardTest, AltLeftStopsAtTheStartOfTheTimeline) {
    ClipKeyboardFixture f;
    f.enter(0); // Bass clip at 0
    EXPECT_TRUE(f.press(key(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier)));
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.bass0)->startBeat, 0.0);
    EXPECT_FALSE(f.undo.canUndo()) << "a move that changes nothing records nothing";
}

TEST(TimelineClipKeyboardTest, AltMoveUsesTheChosenGridDivision) {
    ClipKeyboardFixture f;
    f.panel.getViewState().snap = synth::ui::TimelineViewState::Snap::Sixteenth;
    f.enter(0);
    f.press(key(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier));
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.bass0)->startBeat, 0.25);
}

TEST(TimelineClipKeyboardTest, EscapeLeavesClipModeAndReturnsToTheTracksHeader) {
    ClipKeyboardFixture f;
    f.enter(0);
    f.press(downKey()); // Lead
    ASSERT_EQ(f.panel.getFocusedTrackIndexForTest(), 2);

    EXPECT_TRUE(f.press(escapeKey()));
    EXPECT_FALSE(f.lane().getKeyboardClip().isValid());
    EXPECT_EQ(f.panel.getFocusedTrackIndexForTest(), 2) << "focus goes to the keyboard clip's own track header";
    EXPECT_EQ(f.panel.getClipSelection().size(), 1) << "Escape from clip mode leaves the clip selected";
}

TEST(TimelineClipKeyboardTest, ArrowKeysDoNothingWithoutAKeyboardClipOrSelection) {
    ClipKeyboardFixture f;
    EXPECT_FALSE(f.press(rightKey()));
    EXPECT_FALSE(f.press(returnKey()));
    EXPECT_FALSE(f.press(key(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier)));
}

TEST(TimelineClipKeyboardTest, AClipSelectedWithThePointerCanBeSteppedFromByKeyboard) {
    ClipKeyboardFixture f;
    f.panel.getClipSelection().setSelection({f.bass8});
    EXPECT_TRUE(f.press(rightKey()));
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass16);
}

TEST(TimelineClipKeyboardTest, KeyboardModeEndsWhenTheSelectionChanges) {
    ClipKeyboardFixture f;
    f.enter(0);
    f.panel.getClipSelection().setSelection({f.bass0, f.bass8});
    EXPECT_FALSE(f.lane().getKeyboardClip().isValid());
    EXPECT_TRUE(f.lane().getKeyboardClipAccessibilityText().isEmpty());
}

TEST(TimelineClipKeyboardTest, DeleteStillRemovesTheKeyboardClip) {
    ClipKeyboardFixture f;
    f.enter(0);
    f.press(rightKey());
    EXPECT_TRUE(f.press(key(juce::KeyPress::deleteKey)));
    EXPECT_EQ(f.doc.getClip(f.bass8), nullptr);
    EXPECT_FALSE(f.lane().getKeyboardClip().isValid());
}

TEST(TimelineClipKeyboardTest, SteppingScrollsTheTimelineToKeepTheClipVisible) {
    ClipKeyboardFixture f;
    const auto far = f.doc.addClip(f.bass, 400.0, 4.0, "Far");
    ASSERT_TRUE(far.isValid());
    f.panel.getViewState().firstVisibleBeat = 0.0;
    f.enter(0);
    f.press(rightKey());
    f.press(rightKey());
    f.press(rightKey()); // the clip at 400

    ASSERT_EQ(f.lane().getKeyboardClip(), far);
    const auto& view = f.panel.getViewState();
    const double visibleBeats = (double)f.lane().getWidth() / view.pixelsPerBeat;
    EXPECT_GT(view.firstVisibleBeat, 0.0);
    EXPECT_LE(view.firstVisibleBeat, 400.0);
    EXPECT_GE(view.firstVisibleBeat + visibleBeats, 404.0);
}

TEST(TimelineClipKeyboardTest, SteppingDownScrollsTheTrackRowsToTheClip) {
    ClipKeyboardFixture f;
    TrackId last = f.lead;
    for (int i = 0; i < 20; ++i)
        last = f.doc.addTrack(TrackKind::Midi, "Extra " + juce::String(i));
    const auto clip = f.doc.addClip(last, 9.0, 2.0, "Bottom");
    ASSERT_TRUE(clip.isValid());
    f.panel.setSize(1200, 300);
    ASSERT_DOUBLE_EQ(f.panel.getViewState().trackScrollY, 0.0);

    f.enter(2);
    f.press(downKey());
    EXPECT_EQ(f.lane().getKeyboardClip(), clip);
    EXPECT_GT(f.panel.getViewState().trackScrollY, 0.0);
}

TEST(TimelineClipKeyboardTest, TheLaneAreaReportsTheKeyboardClipToScreenReaders) {
    ClipKeyboardFixture f;
    EXPECT_EQ(f.lane().getTitle(), "Clips");
    EXPECT_TRUE(f.lane().getKeyboardClipAccessibilityText().isEmpty());

    f.enter(0);
    EXPECT_EQ(f.lane().getKeyboardClipAccessibilityText(), "MIDI clip Bassline, track Bass, bars 1 to 2");
    f.press(downKey());
    EXPECT_EQ(f.lane().getKeyboardClipAccessibilityText(), "MIDI clip Lead A, track Lead, from bar 1 beat 3 to bar 2");
}

TEST(TimelineClipKeyboardTest, EveryClipVerbIsRebindable) {
    ClipKeyboardFixture f;
    ShortcutManager manager;
    f.panel.setShortcutManager(&manager);
    f.lane().setShortcutManager(&manager);
    manager.setBinding("timelineClipNext", juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0));
    manager.setBinding("timelineClipOpen", juce::KeyPress('o', juce::ModifierKeys::noModifiers, 0));
    manager.setBinding("timelineClipMoveLater", juce::KeyPress('m', juce::ModifierKeys::altModifier, 0));

    EXPECT_FALSE(f.header(0).keyPressed(rightKey())) << "the old default no longer enters the clips";
    EXPECT_FALSE(f.lane().getKeyboardClip().isValid());
    EXPECT_TRUE(f.header(0).keyPressed(juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0)));
    ASSERT_EQ(f.lane().getKeyboardClip(), f.bass0);

    EXPECT_FALSE(f.press(rightKey()));
    EXPECT_TRUE(f.press(juce::KeyPress('u', juce::ModifierKeys::noModifiers, 0)));
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass8);

    EXPECT_FALSE(f.press(returnKey()));
    EXPECT_FALSE(f.panel.getPianoRoll().isOpen());
    EXPECT_TRUE(f.press(juce::KeyPress('o', juce::ModifierKeys::noModifiers, 0)));
    EXPECT_TRUE(f.panel.getPianoRoll().isOpen());

    f.panel.getPianoRoll().closeRoll();
    EXPECT_FALSE(f.press(key(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier)));
    EXPECT_TRUE(f.press(juce::KeyPress('m', juce::ModifierKeys::altModifier, 0)));
    EXPECT_DOUBLE_EQ(f.doc.getClip(f.bass8)->startBeat, 9.0);

    // The manager is declared after the panel, so it is destroyed first.
    f.panel.setShortcutManager(nullptr);
    f.lane().setShortcutManager(nullptr);
}

// Cmd+Shift+T and Tab leave keyboard focus on the panel root, not on a header; Right has to work
// from there too. Real focus needs a native peer, so this calls the handler the panel's
// keyPressed() runs when it holds focus.
TEST(TimelineClipKeyboardTest, RightOnThePanelRootEntersTheFirstTrackWithClips) {
    ClipKeyboardFixture f;
    EXPECT_TRUE(f.panel.handleRootFocusKey(rightKey()));
    EXPECT_EQ(f.lane().getKeyboardClip(), f.bass0);
}

TEST(TimelineClipKeyboardTest, RightOnThePanelRootEntersTheFocusedTracksClips) {
    ClipKeyboardFixture f;
    f.panel.setRecordFocusForTest(true);                // no native peer: focus moves are recorded, not grabbed
    EXPECT_TRUE(f.panel.handleRootFocusKey(downKey())); // "+ Track", the first stop
    EXPECT_TRUE(f.panel.keyPressed(downKey()));         // row 0
    EXPECT_TRUE(f.panel.getTrackHeaderAt(0)->keyPressed(downKey())); // row 1 (Empty)
    EXPECT_TRUE(f.panel.getTrackHeaderAt(1)->keyPressed(downKey())); // row 2 (Lead)
    EXPECT_TRUE(f.panel.handleRootFocusKey(rightKey()));
    EXPECT_EQ(f.lane().getKeyboardClip(), f.lead2);
}

TEST(TimelineClipKeyboardTest, RootFocusIgnoresOtherKeys) {
    ClipKeyboardFixture f;
    EXPECT_FALSE(f.panel.handleRootFocusKey(leftKey()));
    EXPECT_FALSE(f.panel.handleRootFocusKey(returnKey()));
    EXPECT_FALSE(f.lane().getKeyboardClip().isValid());
}
