// PianoRoll audition tests: NOTE AUDITION (onAuditionNote) — "clicking a note plays it" — through
// the roll's own callback, through the real TimelinePanelComponent -> TrackHeaderHost wiring, and
// the KEYS-COLUMN audition (the virtual keyboard down the left gutter).
// Shared PianoRollFixture, AuditionRecordingHost and AuditionIntegrationFixture live in
// PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

#include "UI/Timeline/EditTool.h"

// ============================================================================
// 20. NOTE AUDITION (onAuditionNote) — "clicking a note plays it".
// ============================================================================
//
// The roll emits a pitch + normalised velocity + an on/off edge and knows nothing about the graph,
// so every one of these tests is a pure callback count. The contract under test is the one a stuck
// note would violate: EXACTLY ONE `false` follows every `true`, on every exit path.

namespace {

// One recorded onAuditionNote call.
struct AuditionEvent {
    int pitch = 0;
    float velocity01 = 0.0f;
    bool on = false;
};

// Wires the roll's callback into `out` and returns nothing — the fixture keeps owning the roll.
void recordAuditionInto(PianoRollFixture& f, std::vector<AuditionEvent>& out) {
    f.roll.onAuditionNote = [&out](int pitch, float velocity01, bool on) { out.push_back({pitch, velocity01, on}); };
}

} // namespace

TEST(PianoRollAuditionTest, MouseDownOnANoteSoundsItAndMouseUpReleasesIt) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    auto n = makeNote(1.0, 64, 1.0);
    n.velocity = 96;
    const auto id = f.doc.addNote(clipId, n);
    ASSERT_TRUE(id.isValid());

    std::vector<AuditionEvent> events;
    recordAuditionInto(f, events);

    const auto anchor = centreOf(f.roll.getNoteRect(id));
    f.roll.mouseDown(leftClick(f.roll, anchor));
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].pitch, 64);
    EXPECT_TRUE(events[0].on);
    EXPECT_NEAR(events[0].velocity01, 96.0f / 127.0f, 1.0e-4f) << "the note's OWN velocity, not a fixed preview level";
    EXPECT_EQ(f.roll.getAuditionPitchForTest(), 64);

    f.roll.mouseUp(leftClick(f.roll, anchor));
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[1].pitch, 64);
    EXPECT_FALSE(events[1].on);
    EXPECT_EQ(f.roll.getAuditionPitchForTest(), -1);
}

TEST(PianoRollAuditionTest, ClickingEmptyGridSoundsNothing) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    ASSERT_TRUE(f.doc.addNote(clipId, makeNote(1.0, 64, 1.0)).isValid());

    std::vector<AuditionEvent> events;
    recordAuditionInto(f, events);

    // Empty grid well clear of the one note, and the header strip.
    const juce::Point<float> empty((float)f.roll.getNoteGridBounds().getCentreX(),
                                   (float)f.roll.getNoteGridBounds().getBottom() - 4.0f);
    f.roll.mouseDown(leftClick(f.roll, empty));
    f.roll.mouseUp(leftClick(f.roll, empty));
    EXPECT_TRUE(events.empty());
}

// A Move drag across pitches re-articulates: off the old pitch, on the new one. Dragging sideways
// inside the SAME row costs nothing — the same state-change gate the repaint invariant demands.
TEST(PianoRollAuditionTest, MoveDragRetriggersOnEachNewPitchAndNotWithinARow) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    f.open(clipId);

    const auto id = f.doc.addNote(clipId, makeNote(2.0, 60, 1.0));
    ASSERT_TRUE(id.isValid());
    f.roll.getSelectionForTest().setSelection({id});

    std::vector<AuditionEvent> events;
    recordAuditionInto(f, events);

    const auto anchor = centreOf(f.roll.getNoteRect(id));
    f.roll.mouseDown(leftClick(f.roll, anchor));
    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].pitch, 60);

    // Sideways only (+1 beat, same row): no new edge at all.
    const juce::Point<float> sideways(anchor.x + 40.0f, anchor.y);
    f.roll.mouseDrag(leftDrag(f.roll, sideways, anchor));
    EXPECT_EQ(events.size(), 1u) << "a drag inside one row must not retrigger";
    EXPECT_EQ(f.roll.getAuditionPitchForTest(), 60);

    // Up two rows (kPixelsPerSemitone == 10): one off + one on, at the NEW pitch.
    const juce::Point<float> up(sideways.x, sideways.y - 20.0f);
    f.roll.mouseDrag(leftDrag(f.roll, up, anchor));
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[1].pitch, 60);
    EXPECT_FALSE(events[1].on) << "the old pitch is released first";
    EXPECT_EQ(events[2].pitch, 62);
    EXPECT_TRUE(events[2].on);
    EXPECT_EQ(f.roll.getAuditionPitchForTest(), 62);

    f.roll.mouseUp(leftDrag(f.roll, up, anchor));
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[3].pitch, 62);
    EXPECT_FALSE(events[3].on) << "the release matches whatever pitch was sounding, not the original";

    // The whole gesture balances: every note-on got exactly one note-off.
    int held = 0;
    for (const auto& e : events)
        held += e.on ? 1 : -1;
    EXPECT_EQ(held, 0);
}

// The no-stuck-note paths, one per cancel route. None of them is a mouseUp.
TEST(PianoRollAuditionTest, EveryCancelPathReleasesTheHeldNote) {
    const auto runCancelCase = [](const std::function<void(PianoRollFixture&)>& cancel) {
        PianoRollFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
        const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
        f.open(clipId);
        const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
        std::vector<AuditionEvent> events;
        recordAuditionInto(f, events);

        f.roll.mouseDown(leftClick(f.roll, centreOf(f.roll.getNoteRect(id))));
        ASSERT_EQ(events.size(), 1u);
        ASSERT_TRUE(f.roll.isAuditionActiveForTest());

        cancel(f);
        ASSERT_EQ(events.size(), 2u);
        EXPECT_FALSE(events[1].on);
        EXPECT_EQ(events[1].pitch, 60);
        EXPECT_FALSE(f.roll.isAuditionActiveForTest());
    };

    // A tool switch abandons the in-flight gesture — and its preview with it.
    runCancelCase([](PianoRollFixture& f) { f.roll.setActiveTool(synth::ui::EditTool::Erase); });
    // Closing the roll: no mouse-up is ever coming.
    runCancelCase([](PianoRollFixture& f) { f.roll.closeRoll(); });
    // Opening a DIFFERENT clip is the same discontinuity.
    runCancelCase([](PianoRollFixture& f) {
        const auto other = f.doc.addClip(f.doc.getTracks().front().id, 8.0, 8.0, "Other");
        f.roll.openClip(other);
    });
    // Being hidden (the panel swapping the clip lanes back in, the window closing).
    runCancelCase([](PianoRollFixture& f) {
        f.roll.setVisible(true);
        f.roll.setVisible(false);
    });
}

// The destructor is the last line of defence: the owner's synth has no way to learn the component
// went away, so the note-off has to come from here.
TEST(PianoRollAuditionTest, DestructorReleasesAHeldNote) {
    TimelineDoc doc;
    TimelineViewState state;
    state.pixelsPerBeat = 40.0;
    state.snap = TimelineViewState::Snap::Quarter;
    AppUndoManager undo;

    const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = doc.addClip(trackId, 0.0, 8.0, "Clip");

    std::vector<AuditionEvent> events;
    {
        PianoRollComponent roll{state};
        roll.setTimelineDoc(&doc);
        roll.setUndoManager(&undo);
        roll.setSize(900, 160);
        roll.openClip(clipId);
        roll.setHorizontalView(40.0, 0.0);
        const auto id = doc.addNote(clipId, makeNote(1.0, 67, 1.0));
        roll.onAuditionNote = [&events](int pitch, float v, bool on) { events.push_back({pitch, v, on}); };
        roll.mouseDown(leftClick(roll, centreOf(roll.getNoteRect(id))));
        ASSERT_EQ(events.size(), 1u);
    }
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[1].pitch, 67);
    EXPECT_FALSE(events[1].on);
}

// Audition belongs to the SELECT tool. An Erase/Mute/Split/Glue click is not a request to hear the
// note it is about to change.
TEST(PianoRollAuditionTest, NonSelectToolClicksDoNotAudition) {
    for (auto tool : {synth::ui::EditTool::Erase, synth::ui::EditTool::Mute, synth::ui::EditTool::Split,
                      synth::ui::EditTool::Glue, synth::ui::EditTool::Draw}) {
        PianoRollFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
        const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
        f.open(clipId);
        const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 2.0));
        ASSERT_TRUE(id.isValid());

        std::vector<AuditionEvent> events;
        recordAuditionInto(f, events);
        f.roll.setActiveTool(tool);

        const auto anchor = centreOf(f.roll.getNoteRect(id));
        f.roll.mouseDown(leftClick(f.roll, anchor));
        f.roll.mouseUp(leftClick(f.roll, anchor));
        EXPECT_TRUE(events.empty()) << "tool " << (int)tool;
    }
}

// ---------------------------------------------------------------------------
// 20b. Audition INTEGRATION — through the real TimelinePanelComponent wiring.
// ---------------------------------------------------------------------------
//
// Everything above wires roll.onAuditionNote straight to a recorder, which tests the ROLL's half of
// the contract and nothing else. The stuck note this section exists for lived in the OTHER half: the
// panel's lambda resolves which track to send to FROM THE OPEN CLIP, so any teardown that clears the
// open clip before the note-off is emitted (or deletes the clip outright) used to drop the off — and
// an audition note is deliberately exempt from every positional flush in TimelineMidiSourceModule, so
// nothing downstream would ever release it. These tests drive the whole chain: roll -> panel ->
// TrackHeaderHost.

TEST(PianoRollAuditionIntegrationTest, PressAndReleaseDeliverOneOnAndOneOffToTheBoundTrack) {
    AuditionIntegrationFixture f;
    ASSERT_TRUE(f.roll().isOpen());
    ASSERT_FALSE(f.roll().getNoteRect(f.noteId).isEmpty()) << "the roll must be laid out for the press to land";

    f.pressAndHoldNote();
    ASSERT_EQ(f.host.onCalls().size(), 1u);
    EXPECT_EQ(f.host.onCalls()[0].track, f.trackId);
    EXPECT_EQ(f.host.onCalls()[0].pitch, 64);
    EXPECT_TRUE(f.host.offCalls().empty());

    auto& r = f.roll();
    r.mouseUp(leftClick(r, centreOf(r.getNoteRect(f.noteId))));
    ASSERT_EQ(f.host.offCalls().size(), 1u);
    EXPECT_EQ(f.host.offCalls()[0].track, f.trackId);
    EXPECT_EQ(f.host.offCalls()[0].pitch, 64);
}

// THE regression: closing the roll mid-hold. closeRoll() clears clipId_, so a note-off emitted after
// that point has no clip to resolve a track from — the off must already have been sent (stopAudition
// runs FIRST) and, either way, the panel routes it to the LATCHED track rather than re-resolving.
TEST(PianoRollAuditionIntegrationTest, ClosingTheRollMidHoldStillDeliversExactlyOneOff) {
    AuditionIntegrationFixture f;
    f.pressAndHoldNote();
    ASSERT_EQ(f.host.onCalls().size(), 1u);
    ASSERT_TRUE(f.host.offCalls().empty());

    f.panel.closePianoRoll();
    ASSERT_FALSE(f.roll().isOpen());

    ASSERT_EQ(f.host.offCalls().size(), 1u) << "a preview cut short by the roll closing must still be released";
    EXPECT_EQ(f.host.offCalls()[0].track, f.trackId) << "routed to the track the ON went to";
    EXPECT_EQ(f.host.offCalls()[0].pitch, 64);
    EXPECT_EQ(f.host.calls.size(), 2u) << "exactly one on and one off, no duplicates";
}

// The same hazard reached the other way: the edited clip is DELETED while the note is held, so
// refreshFromDoc() closes the roll from under the gesture and there is no clip left to resolve at
// all. The latch is what makes this deliverable.
TEST(PianoRollAuditionIntegrationTest, DeletingTheEditedClipMidHoldStillDeliversExactlyOneOff) {
    AuditionIntegrationFixture f;
    f.pressAndHoldNote();
    ASSERT_EQ(f.host.onCalls().size(), 1u);

    ASSERT_TRUE(f.doc.removeClip(f.clipId));
    EXPECT_FALSE(f.roll().isOpen()) << "the roll closes itself when the edited clip disappears";
    EXPECT_EQ(f.doc.getTrackForClip(f.clipId), nullptr) << "and the clip really is gone from the doc";

    ASSERT_EQ(f.host.offCalls().size(), 1u) << "an unresolvable clip must not swallow the note-off";
    EXPECT_EQ(f.host.offCalls()[0].track, f.trackId);
    EXPECT_EQ(f.host.calls.size(), 2u);
}

// Opening a DIFFERENT clip mid-hold: the off must go to the track the ON went to, never to whichever
// track happens to own the newly-opened clip.
TEST(PianoRollAuditionIntegrationTest, OpeningAnotherClipMidHoldRoutesTheOffToTheOriginalTrack) {
    AuditionIntegrationFixture f;
    const auto otherTrack = f.doc.addTrack(TrackKind::Midi, "Track 2");
    const auto otherClip = f.doc.addClip(otherTrack, 16.0, 8.0, "Other");
    ASSERT_NE(f.trackId, otherTrack);

    f.pressAndHoldNote();
    ASSERT_EQ(f.host.onCalls().size(), 1u);

    f.panel.openPianoRoll(otherClip);
    ASSERT_EQ(f.host.offCalls().size(), 1u);
    EXPECT_EQ(f.host.offCalls()[0].track, f.trackId) << "the ON's track, not the newly-opened clip's";
    EXPECT_EQ(f.host.calls.size(), 2u);
}

// A note-OFF with no preceding ON (a stray callback, or one whose ON was refused because the roll
// was closed) must reach the host as NOTHING — there is no note to release, and an unmatched off
// could cut a timeline note of the same pitch short downstream.
TEST(PianoRollAuditionIntegrationTest, AnUnmatchedOffIsNotForwarded) {
    AuditionIntegrationFixture f;
    ASSERT_TRUE(f.roll().onAuditionNote != nullptr);

    f.roll().onAuditionNote(64, 1.0f, false);
    EXPECT_TRUE(f.host.calls.empty());

    // And an ON refused because the roll is closed leaves nothing latched, so its off is inert too.
    f.panel.closePianoRoll();
    f.roll().onAuditionNote(64, 1.0f, true);
    f.roll().onAuditionNote(64, 1.0f, false);
    EXPECT_TRUE(f.host.calls.empty()) << "a closed roll auditions nothing, and releases nothing";
}

// ============================================================================
// 27. KEYS-COLUMN AUDITION — the virtual keyboard down the left gutter.
// ============================================================================

TEST(PianoRollKeysColumnTest, PressingAKeyAuditionsItAndReleasingStopsIt) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    std::vector<AuditionEvent> events;
    recordAuditionInto(f, events);

    const auto keys = f.roll.getKeysColumnBounds();
    ASSERT_FALSE(keys.isEmpty());
    const int pitch = 64;
    const juce::Point<float> onKey((float)keys.getCentreX(), (float)f.roll.yForPitch(pitch) + 2.0f);
    ASSERT_EQ(f.roll.pitchForY((int)onKey.y), pitch) << "the synthesized point really is on that key";

    f.roll.mouseDown(leftClick(f.roll, onKey));
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].pitch, pitch);
    EXPECT_TRUE(events[0].on);
    EXPECT_NEAR(events[0].velocity01, 102.0f / 127.0f, 1.0e-4f) << "a virtual key has no velocity sensor";
    EXPECT_EQ(f.roll.getPressedKeyForTest(), pitch) << "and the key paints pressed";

    f.roll.mouseUp(leftClick(f.roll, onKey));
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[1].pitch, pitch);
    EXPECT_FALSE(events[1].on);
    EXPECT_EQ(f.roll.getPressedKeyForTest(), -1);
}

// Dragging DOWN the keyboard re-articulates on each new key, and costs nothing while the pointer
// stays inside one — the same state-change gate the note drag's retrigger uses.
TEST(PianoRollKeysColumnTest, DraggingAcrossKeysRetriggersOncePerKey) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);

    std::vector<AuditionEvent> events;
    recordAuditionInto(f, events);

    const auto keys = f.roll.getKeysColumnBounds();
    const juce::Point<float> anchor((float)keys.getCentreX(), (float)f.roll.yForPitch(64) + 2.0f);
    f.roll.mouseDown(leftClick(f.roll, anchor));
    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].pitch, 64);

    // Still inside the SAME key row: no MIDI at all.
    f.roll.mouseDrag(leftDrag(f.roll, anchor + juce::Point<float>(0.0f, 1.0f), anchor));
    EXPECT_EQ(events.size(), 1u) << "sliding inside one key must not retrigger";

    // Down two rows (kPixelsPerSemitone == 10 -> 20 px): off the old pitch, on the new one.
    const juce::Point<float> lower(anchor.x, anchor.y + 20.0f);
    f.roll.mouseDrag(leftDrag(f.roll, lower, anchor));
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[1].pitch, 64);
    EXPECT_FALSE(events[1].on) << "the old key is released first";
    EXPECT_EQ(events[2].pitch, 62);
    EXPECT_TRUE(events[2].on);
    EXPECT_EQ(f.roll.getPressedKeyForTest(), 62);

    f.roll.mouseUp(leftDrag(f.roll, lower, anchor));
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[3].pitch, 62);
    EXPECT_FALSE(events[3].on);

    int held = 0;
    for (const auto& e : events)
        held += e.on ? 1 : -1;
    EXPECT_EQ(held, 0) << "every note-on got exactly one note-off";
}

// A keys press starts NO document gesture: no drag mode, no selection change, no undo step.
TEST(PianoRollKeysColumnTest, PressingAKeyEditsNothing) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    const auto id = f.doc.addNote(clipId, makeNote(1.0, 60, 1.0));
    f.roll.getSelectionForTest().setSelection({id});

    const auto keys = f.roll.getKeysColumnBounds();
    const juce::Point<float> onKey((float)keys.getCentreX(), (float)f.roll.yForPitch(72) + 2.0f);
    f.roll.mouseDown(leftClick(f.roll, onKey));
    f.roll.mouseDrag(leftDrag(f.roll, onKey + juce::Point<float>(0.0f, 30.0f), onKey));
    f.roll.mouseUp(leftDrag(f.roll, onKey + juce::Point<float>(0.0f, 30.0f), onKey));

    EXPECT_FALSE(f.roll.isMarqueeActiveForTest());
    EXPECT_TRUE(f.roll.getSelectionForTest().contains(id)) << "the selection is untouched";
    EXPECT_DOUBLE_EQ(f.doc.getNote(id)->startBeat, 1.0);
    EXPECT_FALSE(f.undo.canUndo()) << "a preview is not an edit";
}

// The toolbar row and the ruler band above the keys column are NOT keys.
TEST(PianoRollKeysColumnTest, TheToolbarAndRulerRowsAboveTheColumnAreNotKeys) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    f.roll.setRulerBandHeight(16);

    std::vector<AuditionEvent> events;
    recordAuditionInto(f, events);

    const int columnX = f.roll.getKeysColumnBounds().getCentreX();
    // Inside the toolbar row, left of every chip (the chips start at x == 0, so aim below them in the
    // ruler band instead for the second case).
    f.roll.mouseDown(leftClick(f.roll, {(float)columnX, (float)(PianoRollComponent::kToolbarHeight + 4)}));
    f.roll.mouseUp(leftClick(f.roll, {(float)columnX, (float)(PianoRollComponent::kToolbarHeight + 4)}));
    EXPECT_TRUE(events.empty()) << "the ruler band is the ruler's, not a key";

    // And the first real key row DOES sound, so the guard isn't just swallowing everything.
    const juce::Point<float> firstKey((float)columnX, (float)f.roll.canvasTop() + 2.0f);
    f.roll.mouseDown(leftClick(f.roll, firstKey));
    EXPECT_EQ(events.size(), 1u);
    f.roll.mouseUp(leftClick(f.roll, firstKey));
}

TEST(PianoRollKeysColumnTest, EveryCancelPathReleasesAHeldKey) {
    const auto runCancelCase = [](const std::function<void(PianoRollFixture&)>& cancel) {
        PianoRollFixture f;
        const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
        const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
        f.open(clipId);
        std::vector<AuditionEvent> events;
        recordAuditionInto(f, events);

        const auto keys = f.roll.getKeysColumnBounds();
        f.roll.mouseDown(leftClick(f.roll, {(float)keys.getCentreX(), (float)f.roll.yForPitch(60) + 2.0f}));
        ASSERT_EQ(events.size(), 1u);
        ASSERT_EQ(f.roll.getPressedKeyForTest(), 60);

        cancel(f);
        ASSERT_EQ(events.size(), 2u);
        EXPECT_FALSE(events[1].on);
        EXPECT_EQ(events[1].pitch, 60);
        EXPECT_EQ(f.roll.getPressedKeyForTest(), -1) << "and the key stops painting pressed";
    };

    runCancelCase([](PianoRollFixture& f) { f.roll.closeRoll(); });
    runCancelCase([](PianoRollFixture& f) { f.roll.setActiveTool(synth::ui::EditTool::Erase); });
    runCancelCase([](PianoRollFixture& f) {
        f.roll.setVisible(true);
        f.roll.setVisible(false);
    });
    runCancelCase([](PianoRollFixture& f) {
        const auto other = f.doc.addClip(f.doc.getTracks().front().id, 8.0, 8.0, "Other");
        f.roll.openClip(other);
    });
}

// ---- Through the real panel wiring, to the host recorder ----

TEST(PianoRollAuditionIntegrationTest, AKeysColumnPressReachesTheHostAndReleasesOnMouseUp) {
    AuditionIntegrationFixture f;
    auto& r = f.roll();
    ASSERT_TRUE(r.isOpen());

    const auto keys = r.getKeysColumnBounds();
    ASSERT_FALSE(keys.isEmpty());
    const juce::Point<float> onKey((float)keys.getCentreX(), (float)r.yForPitch(67) + 2.0f);
    ASSERT_EQ(r.pitchForY((int)onKey.y), 67);

    r.mouseDown(leftClick(r, onKey));
    ASSERT_EQ(f.host.onCalls().size(), 1u);
    EXPECT_EQ(f.host.onCalls()[0].track, f.trackId);
    EXPECT_EQ(f.host.onCalls()[0].pitch, 67);
    EXPECT_EQ(f.host.onCalls()[0].velocity, 102);
    EXPECT_TRUE(f.host.offCalls().empty());

    r.mouseUp(leftClick(r, onKey));
    ASSERT_EQ(f.host.offCalls().size(), 1u);
    EXPECT_EQ(f.host.offCalls()[0].track, f.trackId);
    EXPECT_EQ(f.host.offCalls()[0].pitch, 67);
    EXPECT_EQ(f.host.calls.size(), 2u) << "exactly one on and one off";
}

TEST(PianoRollAuditionIntegrationTest, ClosingThePanelMidKeyPressStillDeliversTheOff) {
    AuditionIntegrationFixture f;
    auto& r = f.roll();
    const auto keys = r.getKeysColumnBounds();
    r.mouseDown(leftClick(r, {(float)keys.getCentreX(), (float)r.yForPitch(60) + 2.0f}));
    ASSERT_EQ(f.host.onCalls().size(), 1u);

    f.panel.closePianoRoll();
    ASSERT_EQ(f.host.offCalls().size(), 1u) << "a held virtual key must not survive the roll closing";
    EXPECT_EQ(f.host.offCalls()[0].track, f.trackId);
    EXPECT_EQ(f.host.calls.size(), 2u);
}
