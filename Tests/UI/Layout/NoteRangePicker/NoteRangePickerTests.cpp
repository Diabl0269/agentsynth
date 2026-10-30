// NoteRangePickerTests.cpp -- the shared note range picker: edge drags add or remove notes and snap to the scale, the
// middle moves the range by octaves (by one note with Option), the range stays ordered, and the keys reach every part.
#include "../../ModuleViews/StepGrid/StepGridCardFixture.h"
#include "../../PianoRoll/PianoRollTestHelpers.h"
#include "UI/Layout/NoteRangePicker/NoteRangeGeometry.h"
#include "UI/Layout/NoteRangePicker/NoteRangePicker.h"
#include "UI/ModuleViews/StepGrid/StepGridRandomizePanel.h"
#include <gtest/gtest.h>

using synth::ui::NoteRangePicker;
using Part = NoteRangePicker::Part;

namespace {

juce::MouseEvent mouseAt(NoteRangePicker& comp, float x,
                         juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier) {
    const juce::Point<float> p(x, 17.0f);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false);
}

struct Fixture {
    NoteRangePicker picker;
    int changes = 0;
    int lastLo = -1;
    int lastHi = -1;
    int gestureEnds = 0;
    int gestureLo = -1;
    int gestureHi = -1;

    Fixture() {
        picker.onGestureEnd = [this](int lo, int hi) {
            ++gestureEnds;
            gestureLo = lo;
            gestureHi = hi;
        };
        picker.setSize(280, 34);
        picker.setRange(48, 60); // C3 to C4 when middle C is C4
        picker.onChange = [this](int lo, int hi) {
            ++changes;
            lastLo = lo;
            lastHi = hi;
        };
    }

    float lowEdgeX() { return picker.keyCentreX(picker.getLow()) - picker.whiteKeyWidth() * 0.5f; }
    float highEdgeX() { return picker.keyCentreX(picker.getHigh()) + picker.whiteKeyWidth() * 0.5f; }

    void drag(float fromX, float toX, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier) {
        picker.mouseDown(mouseAt(picker, fromX, mods));
        picker.mouseDrag(mouseAt(picker, (fromX + toX) * 0.5f, mods));
        picker.mouseDrag(mouseAt(picker, toX, mods));
        picker.mouseUp(mouseAt(picker, toX, mods));
    }

    void press(Part part, int key, juce::ModifierKeys mods = {}) {
        picker.getStop(part).keyPressed(juce::KeyPress(key, mods, 0));
    }
};

} // namespace

TEST(NoteRangePicker, SetRangeShowsTheNotesWithoutNotifying) {
    Fixture f;
    f.picker.setRange(60, 48); // reversed
    EXPECT_EQ(f.picker.getLow(), 48);
    EXPECT_EQ(f.picker.getHigh(), 60);
    EXPECT_EQ(f.changes, 0);
    EXPECT_EQ(f.picker.getTitle(), "Note range, C3 to C4");
}

TEST(NoteRangePicker, TheOctavesShownFollowTheRange) {
    Fixture f;
    f.picker.setRange(100, 112);
    EXPECT_LE(f.picker.getWindowBase(), 100);
    EXPECT_GE(f.picker.getWindowBase() + 47, 112);
    f.picker.setRange(0, 12);
    EXPECT_EQ(f.picker.getWindowBase(), 0);
}

TEST(NoteRangePicker, DraggingAnEdgeAddsAndRemovesNotes) {
    Fixture f;
    const int base = f.picker.getWindowBase();
    const float start = f.highEdgeX();
    const float target = f.picker.keyCentreX(67) + f.picker.whiteKeyWidth() * 0.5f; // the edge of G
    ASSERT_GE(67, base);
    f.drag(start, target);
    EXPECT_EQ(f.picker.getHigh(), 67);
    EXPECT_EQ(f.picker.getLow(), 48);
    EXPECT_GE(f.changes, 1);
    EXPECT_EQ(f.lastHi, 67);

    const float lowStart = f.lowEdgeX();
    f.drag(lowStart, f.picker.keyCentreX(52) - f.picker.whiteKeyWidth() * 0.5f);
    EXPECT_EQ(f.picker.getLow(), 52);
}

TEST(NoteRangePicker, AnEdgeDragSnapsToScaleNotes) {
    Fixture f;
    const auto cMajor = synth::makeScale(0, 0);
    f.picker.setScale(&cMajor);
    f.picker.setRange(48, 60);
    // F sharp's black key is the nearest key; F sharp is out of C major, and the tie goes to the lower F.
    const float target = f.picker.keyCentreX(66) + f.picker.whiteKeyWidth() * 0.5f;
    f.drag(f.highEdgeX(), target);
    EXPECT_TRUE(cMajor.contains(f.picker.getHigh()));
    EXPECT_NE(f.picker.getHigh(), 60);
}

TEST(NoteRangePicker, TheRangeStaysOrderedAndKeepsOneNote) {
    Fixture f;
    f.drag(f.highEdgeX(), f.picker.keyCentreX(40)); // far past the low edge
    EXPECT_GE(f.picker.getHigh(), f.picker.getLow());
    f.picker.setRange(48, 60);
    f.drag(f.lowEdgeX(), f.picker.keyCentreX(70));
    EXPECT_LE(f.picker.getLow(), f.picker.getHigh());
    EXPECT_GE(f.picker.getHigh() - f.picker.getLow(), 0);
}

TEST(NoteRangePicker, DraggingTheMiddleMovesByOctaves) {
    Fixture f;
    const float octave = f.picker.whiteKeyWidth() * 7.0f;
    const float mid = (f.lowEdgeX() + f.highEdgeX()) * 0.5f;
    f.drag(mid, mid + octave);
    EXPECT_EQ(f.picker.getLow(), 60);
    EXPECT_EQ(f.picker.getHigh(), 72);
    EXPECT_EQ(f.lastLo, 60);
}

TEST(NoteRangePicker, OptionDraggingTheMiddleMovesByOneNote) {
    Fixture f;
    const float mid = (f.lowEdgeX() + f.highEdgeX()) * 0.5f;
    const float note = f.picker.whiteKeyWidth() * 7.0f / 12.0f;
    f.drag(mid, mid + note,
           juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier));
    EXPECT_EQ(f.picker.getLow(), 49);
    EXPECT_EQ(f.picker.getHigh(), 61);
}

TEST(NoteRangePicker, ThreeTabStopsInOrderEachNamedWithATooltip) {
    Fixture f;
    EXPECT_EQ(f.picker.getStop(Part::LowEdge).getTitle(), "Lowest note");
    EXPECT_EQ(f.picker.getStop(Part::HighEdge).getTitle(), "Highest note");
    EXPECT_EQ(f.picker.getStop(Part::Band).getTitle(), "Move the whole range");
    for (const auto part : {Part::LowEdge, Part::HighEdge, Part::Band}) {
        auto& stop = f.picker.getStop(part);
        EXPECT_TRUE(stop.getWantsKeyboardFocus());
        EXPECT_EQ(dynamic_cast<juce::SettableTooltipClient&>(stop).getTooltip(), stop.getTitle());
    }
    EXPECT_LT(f.picker.getStop(Part::LowEdge).getExplicitFocusOrder(),
              f.picker.getStop(Part::HighEdge).getExplicitFocusOrder());
    EXPECT_LT(f.picker.getStop(Part::HighEdge).getExplicitFocusOrder(),
              f.picker.getStop(Part::Band).getExplicitFocusOrder());
}

TEST(NoteRangePicker, ArrowKeysMoveTheFocusedPartByOneNoteOrAnOctave) {
    Fixture f;
    f.press(Part::LowEdge, juce::KeyPress::rightKey);
    EXPECT_EQ(f.picker.getLow(), 49);
    f.press(Part::HighEdge, juce::KeyPress::leftKey);
    EXPECT_EQ(f.picker.getHigh(), 59);
    f.press(Part::HighEdge, juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier);
    EXPECT_EQ(f.picker.getHigh(), 71);
    f.press(Part::Band, juce::KeyPress::leftKey);
    EXPECT_EQ(f.picker.getLow(), 37);
    EXPECT_EQ(f.picker.getHigh(), 59);
    EXPECT_GE(f.changes, 4);
}

TEST(NoteRangePicker, ArrowKeysStepByScaleNotesAndNeverCrossTheOtherEdge) {
    Fixture f;
    const auto cMajor = synth::makeScale(0, 0);
    f.picker.setScale(&cMajor);
    f.picker.setRange(48, 50);
    f.press(Part::LowEdge, juce::KeyPress::rightKey);
    EXPECT_EQ(f.picker.getLow(), 50); // C to D
    f.press(Part::LowEdge, juce::KeyPress::rightKey);
    EXPECT_EQ(f.picker.getLow(), 50); // cannot pass the high edge
    f.press(Part::HighEdge, juce::KeyPress::leftKey);
    EXPECT_EQ(f.picker.getHigh(), 50);
}

TEST(NoteRangePicker, TheGroupReadsTheRangeAndNamesFollowTheMiddleCOctave) {
    Fixture f;
    f.picker.setMiddleCOctave(3);
    f.picker.setRange(48, 60);
    EXPECT_EQ(f.picker.getTitle(), "Note range, C2 to C3");
}

TEST(NoteRangeGeometry, KeysSitWhereAPianoPutsThem) {
    using namespace synth::ui::noterange;
    const auto c = keySpan(48, 48, 280.0f);
    const auto cSharp = keySpan(49, 48, 280.0f);
    const auto d = keySpan(50, 48, 280.0f);
    EXPECT_FLOAT_EQ(c.x0, 0.0f);
    EXPECT_FLOAT_EQ(cSharp.centre(), c.x1); // a black key sits on the line between its whites
    EXPECT_FLOAT_EQ(d.x0, c.x1);
    EXPECT_EQ(pitchAtX(d.centre(), 48, 280.0f), 50);
    EXPECT_FLOAT_EQ(keySpan(48 + 48, 48, 280.0f).x0, 280.0f); // the next C starts where the strip ends
}

// Regression test for FRO1238: a whole drag is one finished gesture, so the owner records one undo step, not one per
// pointer move.
TEST(NoteRangePicker, AWholeDragFiresGestureEndOnceWithTheFinalRange) {
    Fixture f;
    const float octave = f.picker.whiteKeyWidth() * 7.0f;
    const float mid = (f.lowEdgeX() + f.highEdgeX()) * 0.5f;
    f.drag(mid, mid + octave);
    EXPECT_GE(f.changes, 1);
    EXPECT_EQ(f.gestureEnds, 1);
    EXPECT_EQ(f.gestureLo, 60);
    EXPECT_EQ(f.gestureHi, 72);
}

TEST(NoteRangePicker, AClickThatChangesNothingFiresNoGestureEnd) {
    Fixture f;
    const float mid = (f.lowEdgeX() + f.highEdgeX()) * 0.5f;
    f.drag(mid, mid);
    EXPECT_EQ(f.gestureEnds, 0);
    // A drag that comes back to where it began changed nothing either.
    const float octave = f.picker.whiteKeyWidth() * 7.0f;
    f.picker.mouseDown(mouseAt(f.picker, mid));
    f.picker.mouseDrag(mouseAt(f.picker, mid + octave));
    f.picker.mouseDrag(mouseAt(f.picker, mid));
    f.picker.mouseUp(mouseAt(f.picker, mid));
    EXPECT_EQ(f.gestureEnds, 0);
}

TEST(NoteRangePicker, EachKeyStepThatMovesTheRangeFiresOneGestureEnd) {
    Fixture f;
    f.press(Part::LowEdge, juce::KeyPress::rightKey);
    EXPECT_EQ(f.gestureEnds, 1);
    EXPECT_EQ(f.gestureLo, 49);
    f.press(Part::HighEdge, juce::KeyPress::leftKey);
    EXPECT_EQ(f.gestureEnds, 2);
    f.picker.setRange(0, 5);
    f.press(Part::LowEdge, juce::KeyPress::leftKey); // already at the bottom: nothing moves
    EXPECT_EQ(f.gestureEnds, 2);
}

TEST(NoteRangePicker, SetRangeFiresNoGestureEnd) {
    Fixture f;
    f.picker.setRange(40, 70);
    EXPECT_EQ(f.gestureEnds, 0);
}

// ---- The note range band: saved with its owner and undone one gesture at a time ----

namespace {

using synth::ui::NoteRangePicker;

juce::MouseEvent pickerMouse(NoteRangePicker& picker, float x) {
    const juce::Point<float> p(x, 17.0f);
    const auto mods = juce::ModifierKeys::leftButtonModifier;
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &picker, &picker, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false);
}

// Drags the high edge to the edge of `target` with real mouse events: down, two moves, up.
void dragHighEdgeTo(NoteRangePicker& picker, int target) {
    ASSERT_GT(picker.getWidth(), 0);
    const float half = picker.whiteKeyWidth() * 0.5f;
    const float from = picker.keyCentreX(picker.getHigh()) + half;
    const float to = picker.keyCentreX(target) + half;
    picker.mouseDown(pickerMouse(picker, from));
    picker.mouseDrag(pickerMouse(picker, (from + to) * 0.5f));
    picker.mouseDrag(pickerMouse(picker, to));
    picker.mouseUp(pickerMouse(picker, to));
    ASSERT_EQ(picker.getHigh(), target);
}

} // namespace

// Regression test for FRO1238: the note range of the Randomize panel and of the piano roll's Scale assist panel was
// not part of the saved state, so Cmd+Z skipped it. One guard over both owners: a real drag on the picker, then undo
// restores the range, redo reapplies it, and the drag was exactly one undo step.
TEST(NoteRangeUndoGuard, ADragOnEitherOwnersPickerIsOneUndoStepAndRedoReapplies) {
    struct Owner {
        const char* name;
        std::function<NoteRangePicker&()> picker;
        std::function<std::pair<int, int>()> shown; // what the band shows after the owner refreshed
        std::function<bool()> canUndo;
        std::function<void()> undo;
        std::function<void()> redo;
        std::function<void()> clearHistory;
    };

    // Owner 1: a sequencer card's Randomize popover.
    stepgrid_test::SeqCard card;
    std::unique_ptr<synth::ui::StepGridRandomizePanel> popover;
    const auto openPopover = [&] {
        synth::ui::test_hooks::randomizePanelHookForTest() = [&](auto p) { popover = std::move(p); };
        card.grid->getTools().getButton(synth::ui::StepGridTools::Tool::Randomize).onClick();
        synth::ui::test_hooks::randomizePanelHookForTest() = nullptr;
    };
    openPopover();
    ASSERT_NE(popover, nullptr);

    // Owner 2: the piano roll's Scale assist panel.
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    f.roll.toggleScalePanel();
    auto& panel = f.roll.getScaleAssistPanel();

    const Owner owners[] = {
        {"sequencer Randomize panel", [&]() -> NoteRangePicker& { return popover->getRangePicker(); },
         [&] {
             openPopover(); // reopening reads the pattern's saved range
             return std::make_pair(popover->getRangePicker().getLow(), popover->getRangePicker().getHigh());
         },
         [&] { return card.canvas.undo.canUndo(); }, [&] { card.canvas.undo.getUndoManager().undo(); },
         [&] { card.canvas.undo.getUndoManager().redo(); },
         [&] { card.canvas.undo.getUndoManager().clearUndoHistory(); }},
        {"piano roll Scale assist panel", [&]() -> NoteRangePicker& { return panel.getRangePicker(); },
         [&] { return std::make_pair(panel.getMinPitchSelection(), panel.getMaxPitchSelection()); },
         [&] { return f.undo.canUndo(); },
         [&] {
             f.undo.undo();
             f.roll.refreshFromDoc();
         },
         [&] {
             f.undo.redo();
             f.roll.refreshFromDoc();
         },
         [&] { f.undo.getUndoManager().clearUndoHistory(); }},
    };

    for (const auto& owner : owners) {
        SCOPED_TRACE(owner.name);
        owner.clearHistory();
        const auto start = std::make_pair(owner.picker().getLow(), owner.picker().getHigh());
        const int target = start.second - 3;
        ASSERT_NO_FATAL_FAILURE(dragHighEdgeTo(owner.picker(), target));
        const auto moved = std::make_pair(start.first, target);

        ASSERT_TRUE(owner.canUndo()) << "the drag recorded an undo step";
        owner.undo();
        EXPECT_EQ(owner.shown(), start) << "undo restores the range";
        EXPECT_FALSE(owner.canUndo()) << "a drag is exactly one undo step";
        owner.redo();
        EXPECT_EQ(owner.shown(), moved) << "redo reapplies it";
    }
}

TEST(PianoRollScaleAssistTest, TheNoteRangeSurvivesSaveAndReopenAndAnOldProjectKeepsTheDefaults) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    f.roll.toggleScalePanel();
    ASSERT_NO_FATAL_FAILURE(dragHighEdgeTo(f.roll.getScaleAssistPanel().getRangePicker(), 69));
    ASSERT_TRUE(f.doc.getClip(clipId)->noteRange.has_value());

    const auto reload = [&](const juce::var& saved, TimelineDoc& doc, std::unique_ptr<TimelineViewState>& state,
                            std::unique_ptr<PianoRollComponent>& roll) {
        ASSERT_TRUE(doc.fromVar(saved));
        state = std::make_unique<TimelineViewState>();
        roll = std::make_unique<PianoRollComponent>(*state);
        roll->setTimelineDoc(&doc);
        roll->setSize(900, 160);
        roll->openClip(clipId);
    };

    TimelineDoc reloaded;
    std::unique_ptr<TimelineViewState> state;
    std::unique_ptr<PianoRollComponent> roll;
    ASSERT_NO_FATAL_FAILURE(reload(juce::JSON::parse(juce::JSON::toString(f.doc.toVar())), reloaded, state, roll));
    EXPECT_EQ(reloaded.getClip(clipId)->noteRange, std::make_pair(36, 69));
    EXPECT_EQ(roll->getScaleAssistPanel().getMinPitchSelection(), 36);
    EXPECT_EQ(roll->getScaleAssistPanel().getMaxPitchSelection(), 69);

    // A project saved before the range existed has no "noteRange" key and shows today's C2 to C5.
    auto old = juce::JSON::parse(juce::JSON::toString(f.doc.toVar()));
    for (auto& track : *old.getProperty("tracks", {}).getArray())
        if (auto* clips = track.getProperty("clips", {}).getArray())
            for (auto& clip : *clips)
                clip.getDynamicObject()->removeProperty("noteRange");
    TimelineDoc oldDoc;
    std::unique_ptr<TimelineViewState> oldState;
    std::unique_ptr<PianoRollComponent> oldRoll;
    ASSERT_NO_FATAL_FAILURE(reload(old, oldDoc, oldState, oldRoll));
    EXPECT_FALSE(oldDoc.getClip(clipId)->noteRange.has_value());
    EXPECT_EQ(oldRoll->getScaleAssistPanel().getMinPitchSelection(), 36);
    EXPECT_EQ(oldRoll->getScaleAssistPanel().getMaxPitchSelection(), 72);
}

TEST(PianoRollScaleAssistTest, AKeyChangeMovesTheSavedRangeInTheSameUndoStepAndFollowingTheSongKeyWritesNothing) {
    PianoRollFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clipId = f.doc.addClip(trackId, 0.0, 8.0, "Clip");
    f.open(clipId);
    f.roll.toggleScalePanel();
    auto& panel = f.roll.getScaleAssistPanel();

    // No saved range: a key change shifts the band on screen but writes no range.
    ASSERT_NO_FATAL_FAILURE(pickPanelKey(panel, "d dor"));
    EXPECT_FALSE(f.doc.getClip(clipId)->noteRange.has_value());
    EXPECT_EQ(panel.getMinPitchSelection(), 38) << "C2 moved to D2";
    f.undo.undo();
    f.roll.refreshFromDoc();
    EXPECT_FALSE(f.doc.getClip(clipId)->scale.has_value());
    EXPECT_EQ(panel.getMinPitchSelection(), 36) << "undo shows the default range again";

    // A saved range: the key change carries it, and one undo gives back both.
    f.undo.getUndoManager().clearUndoHistory();
    ASSERT_NO_FATAL_FAILURE(dragHighEdgeTo(panel.getRangePicker(), 70));
    const auto saved = std::make_pair(36, 70);
    ASSERT_EQ(f.doc.getClip(clipId)->noteRange, saved);
    ASSERT_NO_FATAL_FAILURE(pickPanelKey(panel, "d dor"));
    EXPECT_EQ(f.doc.getClip(clipId)->noteRange, std::make_pair(38, 72)) << "moved with the root, in the doc";
    ASSERT_TRUE(f.doc.getClip(clipId)->scale.has_value());

    f.undo.undo();
    f.roll.refreshFromDoc();
    EXPECT_FALSE(f.doc.getClip(clipId)->scale.has_value()) << "one Cmd+Z undoes the key";
    EXPECT_EQ(f.doc.getClip(clipId)->noteRange, saved) << "and the range with it";
    EXPECT_EQ(panel.getMinPitchSelection(), 36);
    EXPECT_EQ(panel.getMaxPitchSelection(), 70);
    f.undo.redo();
    f.roll.refreshFromDoc();
    EXPECT_EQ(f.doc.getClip(clipId)->noteRange, std::make_pair(38, 72));

    // Following the song key again changes the clip's scale as it does today and leaves the saved range alone.
    panel.onFollowSongKey();
    EXPECT_FALSE(f.doc.getClip(clipId)->scale.has_value());
    EXPECT_EQ(f.doc.getClip(clipId)->noteRange, std::make_pair(38, 72));
    EXPECT_EQ(panel.getMinPitchSelection(), 38) << "the saved range stays on screen";
}
