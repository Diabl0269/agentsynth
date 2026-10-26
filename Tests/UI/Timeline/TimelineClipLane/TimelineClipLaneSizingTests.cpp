// TimelineClipLaneSizingTests.cpp — clip edge sizing through the real mouse path: Trim, Carry contents and Stretch to
// fit on both edges, Option for a one-off stretch, the readout, linked copies, and the preview-equals-commit guard.

#include "TimelineClipLaneTestFixture.h"

using synth::ClipSizingMode;

namespace {

constexpr ClipSizingMode kAllModes[] = {ClipSizingMode::Trim, ClipSizingMode::CarryContents, ClipSizingMode::Stretch};

// A [4, 8) clip with notes at clip-relative 0, 1.5 and 3 (absolute 4, 5.5 and 7).
ClipId addNotedClip(ClipLaneFixture& f) {
    const auto track = f.doc.addTrack(TrackKind::Midi, "Track 1");
    const auto clip = f.doc.addClip(track, 4.0, 4.0, "Clip");
    f.doc.addNote(clip, makeNote(0.0, 60, 1.0));
    f.doc.addNote(clip, makeNote(1.5, 62, 1.0));
    f.doc.addNote(clip, makeNote(3.0, 64, 1.0));
    return clip;
}

std::vector<double> relativeStarts(const synth::Clip& clip) {
    std::vector<double> starts;
    for (const auto& note : clip.notes)
        starts.push_back(note.startBeat);
    return starts;
}

// The lane rendered in software (a GPU-backed image reads back zeros on some platforms).
juce::Image renderLane(TimelineClipLaneArea& lane) {
    juce::Image image(juce::Image::ARGB, lane.getWidth(), lane.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    lane.paintEntireComponent(g, true);
    return image;
}

bool samePixelsBelow(const juce::Image& a, const juce::Image& b, int fromY) {
    for (int y = fromY; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

} // namespace

// The guard for every mode, both edges, shrink and grow: the drop leaves each clip exactly as the drag showed it.
TEST(TimelineClipLaneSizingTest, EveryModeOnEitherEdgeCommitsWhatItPreviews) {
    for (const auto mode : kAllModes)
        for (const bool left : {true, false})
            for (const double beats : {1.0, -1.0}) {
                SCOPED_TRACE(::testing::Message()
                             << synth::clipSizingModeName(mode) << (left ? " left " : " right ") << beats);
                ClipLaneFixture f;
                const auto clip = addNotedClip(f);
                f.lane.setClipSizingMode(mode);
                dragEdgeExpectingPreviewEqualsCommit(f, clip, left, beats);
                EXPECT_FALSE(f.lane.isDragInProgress());
            }
}

// Regression test for FRO1096: the left-edge preview carried the notes along with the edge while the drop kept them
// on their beat, so they jumped on release.
TEST(TimelineClipLaneSizingTest, TrimLeftEdgeKeepsNotesOnTheirBeatWhileDragging) {
    ClipLaneFixture f;
    const auto clip = addNotedClip(f);
    const auto shown = dragEdgeExpectingPreviewEqualsCommit(f, clip, true, 1.0); // start 4 -> 5

    const auto& notes = shown.at(clip.value).notes;
    ASSERT_EQ(notes.size(), 2u) << "the note wholly before the new edge is gone";
    EXPECT_DOUBLE_EQ(std::get<0>(notes[0]), 5.5);
    EXPECT_DOUBLE_EQ(std::get<0>(notes[1]), 7.0);
}

TEST(TimelineClipLaneSizingTest, CarryContentsLeftEdgeMovesTheNotesWithTheEdge) {
    ClipLaneFixture f;
    const auto clip = addNotedClip(f);
    f.lane.setClipSizingMode(ClipSizingMode::CarryContents);
    dragEdgeExpectingPreviewEqualsCommit(f, clip, true, 1.0);

    const auto* c = f.doc.getClip(clip);
    EXPECT_DOUBLE_EQ(c->startBeat, 5.0);
    EXPECT_DOUBLE_EQ(c->lengthBeats, 3.0);
    EXPECT_EQ(relativeStarts(*c), (std::vector<double>{0.0, 1.5, 3.0})) << "notes keep their place inside the clip";
}

TEST(TimelineClipLaneSizingTest, StretchScalesTheNotesFromEitherEdge) {
    for (const bool left : {true, false}) {
        ClipLaneFixture f;
        const auto clip = addNotedClip(f);
        f.lane.setClipSizingMode(ClipSizingMode::Stretch);
        dragEdgeExpectingPreviewEqualsCommit(f, clip, left, left ? 1.0 : -1.0); // 4 beats -> 3

        const auto* c = f.doc.getClip(clip);
        EXPECT_DOUBLE_EQ(c->startBeat, left ? 5.0 : 4.0) << left;
        EXPECT_DOUBLE_EQ(c->lengthBeats, 3.0) << left;
        EXPECT_EQ(relativeStarts(*c), (std::vector<double>{0.0, 1.125, 2.25})) << left;
        EXPECT_DOUBLE_EQ(c->notes[1].lengthBeats, 0.75) << left;
    }
}

TEST(TimelineClipLaneSizingTest, OptionStretchesOneDragAndTheNextDragTrimsAgain) {
    ClipLaneFixture f;
    const auto clip = addNotedClip(f);
    ASSERT_EQ(f.lane.getClipSizingMode(), ClipSizingMode::Trim);

    dragEdgeExpectingPreviewEqualsCommit(f, clip, false, -1.0, juce::ModifierKeys::altModifier);
    EXPECT_EQ(relativeStarts(*f.doc.getClip(clip)), (std::vector<double>{0.0, 1.125, 2.25}));
    EXPECT_EQ(f.lane.getClipSizingMode(), ClipSizingMode::Trim) << "Option never changes the setting";

    dragEdgeExpectingPreviewEqualsCommit(f, clip, false, -1.0);
    EXPECT_DOUBLE_EQ(f.doc.getClip(clip)->lengthBeats, 2.0);
    EXPECT_EQ(relativeStarts(*f.doc.getClip(clip)), (std::vector<double>{0.0, 1.125, 2.25})) << "a plain trim";
}

TEST(TimelineClipLaneSizingTest, PressingOptionMidDragTurnsItIntoAStretchAndReleasingItTurnsItBack) {
    ClipLaneFixture f;
    const auto clip = addNotedClip(f);
    const auto rect = f.lane.getClipRect(clip);
    const juce::Point<float> edge((float)rect.getRight() - 3.0f, (float)rect.getCentreY());
    const juce::Point<float> dragged(edge.x - 40.0f, edge.y);
    f.lane.mouseDown(leftClick(f.lane, edge));
    f.lane.mouseDrag(leftDrag(f.lane, dragged, edge));
    EXPECT_TRUE(f.lane.getSizingReadoutForTest().isEmpty()) << "Trim says nothing";

    f.lane.modifierKeysChanged(
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier));
    EXPECT_EQ(f.lane.getSizingReadoutForTest(), "75%");
    EXPECT_DOUBLE_EQ(f.lane.resizePreviewFor(*f.doc.getClip(clip))->notes[1].startBeat, 1.125);

    f.lane.modifierKeysChanged(juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier));
    EXPECT_DOUBLE_EQ(f.lane.resizePreviewFor(*f.doc.getClip(clip))->notes[1].startBeat, 1.5);
    f.lane.mouseUp(leftDrag(f.lane, dragged, edge));
    EXPECT_EQ(relativeStarts(*f.doc.getClip(clip)), (std::vector<double>{0.0, 1.5, 3.0}));
    EXPECT_TRUE(f.lane.getSizingReadoutForTest().isEmpty()) << "the readout ends with the drag";
}

TEST(TimelineClipLaneSizingTest, StretchIsOneUndoStep) {
    ClipLaneFixture f;
    const auto clip = addNotedClip(f);
    f.lane.setClipSizingMode(ClipSizingMode::Stretch);
    dragEdgeExpectingPreviewEqualsCommit(f, clip, true, -2.0); // 4 beats -> 6

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    const auto* c = f.doc.getClip(clip);
    EXPECT_DOUBLE_EQ(c->startBeat, 4.0);
    EXPECT_DOUBLE_EQ(c->lengthBeats, 4.0);
    EXPECT_EQ(relativeStarts(*c), (std::vector<double>{0.0, 1.5, 3.0}));
    EXPECT_DOUBLE_EQ(c->notes[0].lengthBeats, 1.0);
}

namespace {

// A [4, 8) audio clip on its own audio track.
ClipId addAudioClip(ClipLaneFixture& f) {
    const auto track = f.doc.addTrack(TrackKind::Audio, "Audio 1");
    const auto clip = f.doc.addClip(track, 4.0, 4.0, "take");
    EXPECT_TRUE(f.doc.setClipAsset(clip, "Audio/take.wav", 0.0));
    return clip;
}

struct StretchRequest {
    ClipId id;
    double startBeat;
    double lengthBeats;
};

// Drags the clip's left or right edge 40 px (one beat) inwards with `extraFlags` held, checking the readout mid-drag.
void dragAudioEdgeInwards(ClipLaneFixture& f, ClipId clip, bool left, const juce::String& readout, int extraFlags = 0) {
    const auto rect = f.lane.getClipRect(clip);
    const juce::Point<float> edge(left ? (float)rect.getX() + 3.0f : (float)rect.getRight() - 3.0f,
                                  (float)rect.getCentreY());
    const juce::Point<float> dragged(edge.x + (left ? 40.0f : -40.0f), edge.y);
    f.lane.mouseDown(leftClick(f.lane, edge, extraFlags));
    f.lane.mouseDrag(leftDrag(f.lane, dragged, edge, extraFlags));
    EXPECT_EQ(f.lane.getSizingReadoutForTest(), readout);
    f.lane.mouseUp(leftDrag(f.lane, dragged, edge, extraFlags));
}

} // namespace

// The stretch itself is a rendered copy of the audio that only the owner can import (MainComponentClipStretch.cpp), so
// the lane hands it the drop's span and leaves the clip alone.
TEST(TimelineClipLaneSizingTest, AnAudioClipStretchDropAsksTheOwnerWithItsNewSpan) {
    for (const bool left : {true, false}) {
        SCOPED_TRACE(left ? "left" : "right");
        ClipLaneFixture f;
        const auto clip = addAudioClip(f);
        f.lane.setClipSizingMode(ClipSizingMode::Stretch);
        std::vector<StretchRequest> requests;
        f.lane.onAudioClipStretchRequested = [&](ClipId id, double start, double length) {
            requests.push_back({id, start, length});
        };

        dragAudioEdgeInwards(f, clip, left, "75%");

        ASSERT_EQ(requests.size(), 1u);
        EXPECT_EQ(requests[0].id, clip);
        EXPECT_DOUBLE_EQ(requests[0].startBeat, left ? 5.0 : 4.0);
        EXPECT_DOUBLE_EQ(requests[0].lengthBeats, 3.0);
        EXPECT_DOUBLE_EQ(f.doc.getClip(clip)->lengthBeats, 4.0) << "the owner applies it";
        EXPECT_FALSE(f.undo.canUndo()) << "and records the undo step";
    }
}

TEST(TimelineClipLaneSizingTest, OptionStretchesAnAudioClipOnceUnderTrim) {
    ClipLaneFixture f;
    const auto clip = addAudioClip(f);
    int requests = 0;
    f.lane.onAudioClipStretchRequested = [&](ClipId, double, double) { ++requests; };

    dragAudioEdgeInwards(f, clip, false, "75%", juce::ModifierKeys::altModifier);
    EXPECT_EQ(requests, 1);

    dragAudioEdgeInwards(f, clip, false, {});
    EXPECT_EQ(requests, 1) << "a plain drag trims";
    EXPECT_DOUBLE_EQ(f.doc.getClip(clip)->lengthBeats, 3.0);
}

// With no owner to render (a lane on its own), a Stretch drop on audio still resizes, as before audio could stretch.
TEST(TimelineClipLaneSizingTest, AnAudioClipWithNoStretchOwnerResizesInstead) {
    ClipLaneFixture f;
    const auto clip = addAudioClip(f);
    f.lane.setClipSizingMode(ClipSizingMode::Stretch);

    dragAudioEdgeInwards(f, clip, false, "75%");

    const auto* c = f.doc.getClip(clip);
    EXPECT_DOUBLE_EQ(c->lengthBeats, 3.0);
    EXPECT_DOUBLE_EQ(c->sourceStartSeconds, 0.0);
}

// A linked copy shares notes and length: it previews the stretch and lands with it; a left Trim unlinks the dragged
// clip and leaves the copy alone, in the preview as on release.
TEST(TimelineClipLaneSizingTest, ALinkedCopyFollowsAStretchButNotALeftTrim) {
    for (const auto mode : {ClipSizingMode::Stretch, ClipSizingMode::Trim}) {
        ClipLaneFixture f;
        const auto clip = addNotedClip(f);
        const auto copy = f.doc.duplicateClipLinked(clip);
        ASSERT_TRUE(copy.isValid());
        f.lane.setClipSizingMode(mode);
        dragEdgeExpectingPreviewEqualsCommit(f, clip, true, 1.0);

        const auto* c = f.doc.getClip(copy);
        if (mode == ClipSizingMode::Stretch) {
            EXPECT_DOUBLE_EQ(c->lengthBeats, 3.0);
            EXPECT_EQ(relativeStarts(*c), (std::vector<double>{0.0, 1.125, 2.25}));
        } else {
            EXPECT_DOUBLE_EQ(c->lengthBeats, 4.0);
            EXPECT_EQ(relativeStarts(*c), (std::vector<double>{0.0, 1.5, 3.0}));
            EXPECT_EQ(f.doc.getClip(clip)->linkGroup, 0);
        }
    }
}

// The painted frame: the last drag frame and the frame after release are the same pixels (the Stretch readout pill,
// drawn only while dragging, sits in the clip's top band and is left out).
TEST(TimelineClipLaneSizingTest, TheLastDragFrameIsTheFrameAfterRelease) {
    for (const auto mode : kAllModes)
        for (const bool left : {true, false}) {
            SCOPED_TRACE(::testing::Message() << synth::clipSizingModeName(mode) << (left ? " left" : " right"));
            ClipLaneFixture f;
            const auto clip = addNotedClip(f);
            f.lane.setClipSizingMode(mode);
            const auto rect = f.lane.getClipRect(clip);
            const juce::Point<float> edge(left ? (float)rect.getX() + 3.0f : (float)rect.getRight() - 3.0f,
                                          (float)rect.getCentreY());
            const juce::Point<float> dragged(edge.x + (left ? 40.0f : -40.0f), edge.y);
            f.lane.mouseDown(leftClick(f.lane, edge));
            const auto pressed = renderLane(f.lane);
            f.lane.mouseDrag(leftDrag(f.lane, dragged, edge));
            const auto dragging = renderLane(f.lane);
            f.lane.mouseUp(leftDrag(f.lane, dragged, edge));
            const auto released = renderLane(f.lane);

            const int belowReadout = rect.getY() + 20;
            EXPECT_FALSE(samePixelsBelow(pressed, dragging, belowReadout)) << "the drag must show on screen";
            EXPECT_TRUE(samePixelsBelow(dragging, released, belowReadout));
        }
}
