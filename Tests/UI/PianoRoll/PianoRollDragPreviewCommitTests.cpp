// PianoRoll drags commit what they preview: a real mouse-down, several drag events (pointer off the grid lines, so a
// snap bug would show), the geometry / velocity the roll shows after the LAST drag event, then the release and the
// notes the document holds. Covers the note move, the right-edge resize, the Option copy ghosts and the velocity
// strip's stick and Shift ramp. (The draw tool is guarded in PianoRollEditToolsTests.cpp.)

#include "../Layout/ReorderDragPreviewCommit.h"
#include "PianoRollVelocityTestHelpers.h"

#include <algorithm>
#include <tuple>

namespace {

using Shape = std::tuple<double, double, int>; // start, length, pitch

std::vector<Shape> sortedShapes(std::vector<Shape> shapes) {
    std::sort(shapes.begin(), shapes.end());
    return shapes;
}

std::vector<Shape> committedShapes(PianoRollFixture& f, ClipId clipId) {
    std::vector<Shape> shapes;
    for (const auto& note : f.doc.getClip(clipId)->notes)
        shapes.emplace_back(note.startBeat, note.lengthBeats, note.pitch);
    return sortedShapes(shapes);
}

struct Bed {
    ClipId clipId;
    NoteId a, b;
};

// Two notes on different rows in a clip long enough to drag right; both selected so a drag carries the pair.
Bed makeBed(PianoRollFixture& f) {
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Track 1");
    Bed bed;
    bed.clipId = f.doc.addClip(trackId, 0.0, 16.0, "Clip");
    bed.a = f.doc.addNote(bed.clipId, makeNote(2.0, 60, 1.0));
    bed.b = f.doc.addNote(bed.clipId, makeNote(4.0, 64, 2.0));
    setSnap(f, TimelineViewState::Snap::Quarter);
    f.open(bed.clipId);
    f.roll.getSelectionForTest().setSelection({bed.a, bed.b});
    return bed;
}

// Several drag events ending off the grid lines (+1.37 beats, two rows up), then the release.
std::vector<juce::Point<float>> wanderingPath(juce::Point<float> from, float dx, float dy) {
    return reorder_preview_commit::straightPath(from, {from.x + dx, from.y + dy}, 7);
}

} // namespace

TEST(PianoRollDragPreviewCommitTest, ANoteMoveCommitsTheStartsAndPitchesItLastShowed) {
    PianoRollFixture f;
    const auto bed = makeBed(f);
    const auto press = centreOf(f.roll.getNoteRect(bed.a));
    const auto path = wanderingPath(press, 1.37f * 40.0f, -2.0f * 10.0f);

    f.roll.mouseDown(leftClick(f.roll, press));
    for (const auto& point : path)
        f.roll.mouseDrag(leftDrag(f.roll, point, press));
    std::vector<Shape> shown;
    for (const auto id : {bed.a, bed.b}) {
        const auto geometry = f.roll.getShownGeometryForTest(id);
        shown.emplace_back(geometry.startBeat, geometry.lengthBeats, geometry.pitch);
    }
    f.roll.mouseUp(leftDrag(f.roll, path.back(), press));

    const auto committed = committedShapes(f, bed.clipId);
    EXPECT_EQ(sortedShapes(shown), committed) << "what the last drag frame showed is what the release wrote";
    EXPECT_NE(committed, (std::vector<Shape>{{2.0, 1.0, 60}, {4.0, 2.0, 64}})) << "the drag really moved the notes";
}

TEST(PianoRollDragPreviewCommitTest, ARightEdgeResizeCommitsTheLengthsItLastShowed) {
    PianoRollFixture f;
    const auto bed = makeBed(f);
    const auto rect = f.roll.getNoteRect(bed.a);
    const juce::Point<float> press((float)rect.getRight() - 2.0f, (float)rect.getCentreY());
    const auto path = wanderingPath(press, 1.37f * 40.0f, 0.0f);

    f.roll.mouseDown(leftClick(f.roll, press));
    for (const auto& point : path)
        f.roll.mouseDrag(leftDrag(f.roll, point, press));
    ASSERT_GT(f.roll.getResizeNoteCountForTest(), 0) << "the press landed on the edge";
    std::vector<Shape> shown;
    for (const auto id : {bed.a, bed.b}) {
        const auto geometry = f.roll.getShownGeometryForTest(id);
        shown.emplace_back(geometry.startBeat, geometry.lengthBeats, geometry.pitch);
    }
    f.roll.mouseUp(leftDrag(f.roll, path.back(), press));

    const auto committed = committedShapes(f, bed.clipId);
    EXPECT_EQ(sortedShapes(shown), committed) << "what the last drag frame showed is what the release wrote";
    EXPECT_NE(committed, (std::vector<Shape>{{2.0, 1.0, 60}, {4.0, 2.0, 64}})) << "the drag really resized the notes";
}

TEST(PianoRollDragPreviewCommitTest, AnOptionDragCopyCommitsTheNotesItsGhostsLastShowed) {
    PianoRollFixture f;
    const auto bed = makeBed(f);
    const auto originals = committedShapes(f, bed.clipId);
    const auto press = centreOf(f.roll.getNoteRect(bed.a));
    const auto path = wanderingPath(press, 1.37f * 40.0f, -2.0f * 10.0f);
    constexpr int kAlt = juce::ModifierKeys::altModifier;

    f.roll.mouseDown(leftClick(f.roll, press, kAlt));
    for (const auto& point : path)
        f.roll.mouseDrag(leftDrag(f.roll, point, press, kAlt));
    ASSERT_TRUE(f.roll.isCopyDragForTest());
    std::vector<Shape> shown = originals; // the originals stay put under the ghosts
    for (const auto& ghost : f.roll.getCopyGhostsForTest())
        shown.emplace_back(ghost.startBeat, ghost.lengthBeats, ghost.pitch);
    f.roll.mouseUp(leftDrag(f.roll, path.back(), press, kAlt));

    const auto committed = committedShapes(f, bed.clipId);
    ASSERT_EQ(committed.size(), 4u);
    EXPECT_EQ(sortedShapes(shown), committed) << "the originals plus the ghosts as last drawn are what the drop wrote";
}

TEST(PianoRollVelocityDragPreviewCommitTest, AStickDragCommitsTheVelocitiesItLastShowed) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 60, 120, 80});
    f.roll.getSelectionForTest().setSelection({bed.notes[0], bed.notes[1]});
    auto& lane = f.lane();
    const auto press = f.at(1.0, 100);
    const auto path = reorder_preview_commit::straightPath(press, f.at(1.0, 43), 7);

    lane.mouseDown(leftClick(lane, press));
    for (const auto& point : path)
        lane.mouseDrag(leftDrag(lane, point, press));
    std::vector<int> shown;
    for (const auto id : bed.notes)
        shown.push_back(f.roll.getShownGeometryForTest(id).velocity);
    lane.mouseUp(leftDrag(lane, path.back(), press));

    std::vector<int> committed;
    for (const auto id : bed.notes)
        committed.push_back(f.velocityOf(id));
    EXPECT_EQ(shown, committed);
    EXPECT_NE(committed[0], 100) << "the drag really changed the velocity";
}

TEST(PianoRollVelocityDragPreviewCommitTest, AShiftRampCommitsTheVelocitiesItLastShowed) {
    VelocityLaneFixture f;
    const auto bed = f.makeBed({100, 100, 100, 100});
    auto& lane = f.lane();
    const auto anchor = f.at(0.5, 10);
    constexpr int kShift = juce::ModifierKeys::shiftModifier;
    const auto path = reorder_preview_commit::straightPath(anchor, f.at(3.7, 77), 7);

    lane.mouseDown(leftClick(lane, anchor, kShift));
    for (const auto& point : path)
        lane.mouseDrag(leftDrag(lane, point, anchor, kShift));
    std::vector<int> shown;
    for (const auto id : bed.notes)
        shown.push_back(f.roll.getShownGeometryForTest(id).velocity);
    lane.mouseUp(leftDrag(lane, path.back(), anchor, kShift));

    std::vector<int> committed;
    for (const auto id : bed.notes)
        committed.push_back(f.velocityOf(id));
    EXPECT_EQ(shown, committed);
    EXPECT_NE(committed[0], 100) << "the ramp really changed the velocities";
}
