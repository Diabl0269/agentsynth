// TimelineSectionStripDragTests.cpp -- moving and placing again by dragging a block: synthesised MouseEvents on the
// strip's real handlers, the drop modes, Esc, one undo step per drop, and the bar / edge snapping.
#include "TimelineSectionStripFixture.h"
#include <gtest/gtest.h>

using namespace sectionstrip_test;

namespace {

// Intro over beats 0..8 (a clip at 0), Verse over 8..16 (a clip at 8).
struct TwoSections : StripFixture {
    PlacementId intro, verse;
    ClipId introClip, verseClip;

    TwoSections() {
        introClip = doc.addClip(track, 0.0, 4.0, "Intro clip");
        verseClip = doc.addClip(track, 8.0, 4.0, "Verse clip");
        doc.makeSection(0.0, 8.0, "Intro", 0xff56D4C0, 0.5);
        doc.makeSection(8.0, 16.0, "Verse", 0xff4FC1FF, 0.5);
        strip.refresh();
        intro = doc.getPlacements()[0].id;
        verse = doc.getPlacements()[1].id;
    }

    // Presses `grabBeats` into the block that starts at `blockStart`, drags until its left edge is over `toBeat`.
    void dragBlock(double blockStart, double toBeat, int downFlags = 0, int upFlags = -1, float releaseY = 12.0f) {
        const double grab = 2.0;
        const juce::Point<float> from{x(blockStart + grab), 12.0f};
        const juce::Point<float> to{x(toBeat + grab), releaseY};
        strip.mouseDown(event(from, from, false, downFlags));
        strip.mouseDrag(event(to, from, true, downFlags));
        strip.mouseUp(event(to, from, true, upFlags < 0 ? downFlags : upFlags));
    }
    double startOf(PlacementId id) const { return doc.getPlacement(id)->startBeat; }
};

constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kShift = juce::ModifierKeys::shiftModifier;

} // namespace

TEST(TimelineSectionStripDragTest, ADragMovesTheBlockWithItsMusicAsOneUndoStep) {
    TwoSections f;
    f.dragBlock(0.0, 16.0); // the drop line at the Verse's end: the Intro goes after the Verse
    EXPECT_DOUBLE_EQ(f.startOf(f.intro), 8.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.verse), 0.0) << "the gap it left closes";
    EXPECT_EQ(f.doc.getPlacements().size(), 2u) << "a move never adds a placement";
    EXPECT_TRUE(f.undo.undo());
    EXPECT_DOUBLE_EQ(f.startOf(f.intro), 0.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.verse), 8.0);
    EXPECT_FALSE(f.undo.undo()) << "the drop was one step";
}

TEST(TimelineSectionStripDragTest, MakeRoomMovesWhatIsUnderTheDropLater) {
    TwoSections f;
    f.dragBlock(8.0, 0.0); // the Verse onto the Intro
    EXPECT_DOUBLE_EQ(f.startOf(f.verse), 0.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.intro), 8.0) << "the Intro is pushed later, not covered";
}

TEST(TimelineSectionStripDragTest, CoverSettingOverwritesWhatIsUnderTheDropAndShiftFlipsEachWay) {
    {
        TwoSections f;
        f.dragBlock(0.0, 8.0, 0, kShift); // Shift at release flips make-room to cover
        EXPECT_DOUBLE_EQ(f.startOf(f.intro), 8.0);
        EXPECT_EQ(f.doc.getPlacement(f.verse), nullptr) << "Cover removes the Verse it landed on";
    }
    {
        TwoSections f;
        juce::File dir = juce::File::createTempFile("sectionstrip");
        dir.createDirectory();
        juce::PropertiesFile::Options options;
        options.filenameSuffix = ".settings";
        options.folderName = dir.getFullPathName();
        juce::PropertiesFile props(dir.getChildFile("p.settings"), options);
        props.setValue("timelineSectionsCoverOnDrop", true);
        f.strip.setPropertiesFile(&props);
        EXPECT_EQ(f.strip.dropMode(), SectionDropMode::Cover);
        EXPECT_EQ(f.strip.dropMode(true), SectionDropMode::MakeRoom);
        f.dragBlock(0.0, 8.0, 0, kShift); // flipped back to make-room
        EXPECT_NE(f.doc.getPlacement(f.verse), nullptr);
        f.strip.setPropertiesFile(nullptr);
        dir.deleteRecursively();
    }
}

TEST(TimelineSectionStripDragTest, OptionDragPlacesTheSectionAgainAndTheCopyFollowsEdits) {
    TwoSections f;
    f.dragBlock(0.0, 24.0, kAlt);
    ASSERT_EQ(f.doc.getPlacements().size(), 3u);
    EXPECT_DOUBLE_EQ(f.startOf(f.intro), 0.0) << "the original stays";
    const auto& copy = f.doc.getPlacements().back();
    EXPECT_DOUBLE_EQ(copy.startBeat, 24.0);
    EXPECT_EQ(copy.section, f.doc.getPlacement(f.intro)->section) << "a copy is the same section";
    EXPECT_EQ(f.doc.placementsOf(copy.section).size(), 2u);
    EXPECT_EQ(f.strip.getSelectedPlacement(), copy.id) << "the new block is selected";

    const auto* track = f.doc.getTrack(f.track);
    ASSERT_NE(track, nullptr);
    ClipId copied;
    for (const auto& clip : track->clips)
        if (clip.startBeat == 24.0)
            copied = clip.id;
    ASSERT_TRUE(copied.isValid());
    f.doc.setClipName(copied, "Edited");
    EXPECT_EQ(f.doc.getClip(f.introClip)->name, "Edited") << "an edit in one placement reaches the other";
}

TEST(TimelineSectionStripDragTest, OptionDragIsOneUndoStep) {
    TwoSections f;
    f.dragBlock(0.0, 24.0, kAlt);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getPlacements().size(), 2u);
    EXPECT_FALSE(f.undo.undo());
}

TEST(TimelineSectionStripDragTest, EscMidDragCancelsWithNoDocChangeAndNoUndoEntry) {
    TwoSections f;
    const juce::Point<float> from{f.x(2.0), 12.0f}, to{f.x(26.0), 12.0f};
    f.strip.mouseDown(f.event(from, from, false));
    f.strip.mouseDrag(f.event(to, from, true));
    ASSERT_TRUE(f.strip.isBlockDraggingForTest());
    EXPECT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.strip.isBlockDraggingForTest());
    f.strip.mouseDrag(f.event({f.x(30.0), 12.0f}, from, true)); // the rest of the press is ignored
    EXPECT_FALSE(f.strip.isBlockDraggingForTest());
    f.strip.mouseUp(f.event(to, from, true));
    EXPECT_DOUBLE_EQ(f.startOf(f.intro), 0.0);
    EXPECT_FALSE(f.undo.undo()) << "no undo entry was made";
}

TEST(TimelineSectionStripDragTest, ReleasingAboveTheStripCancels) {
    TwoSections f;
    f.dragBlock(0.0, 24.0, 0, -1, -10.0f);
    EXPECT_DOUBLE_EQ(f.startOf(f.intro), 0.0);
    EXPECT_FALSE(f.undo.undo());
}

TEST(TimelineSectionStripDragTest, AClickOrASmallWobbleDoesNotMoveAnything) {
    TwoSections f;
    const juce::Point<float> from{f.x(2.0), 12.0f}, near{f.x(2.0) + 2.0f, 12.0f};
    f.strip.mouseDown(f.event(from, from, false));
    f.strip.mouseDrag(f.event(near, from, true));
    EXPECT_FALSE(f.strip.isBlockDraggingForTest());
    f.strip.mouseUp(f.event(near, from, true));
    EXPECT_DOUBLE_EQ(f.startOf(f.intro), 0.0);
    EXPECT_EQ(f.strip.getSelectedPlacement(), f.intro);
}

TEST(TimelineSectionStripDragTest, TheDropBeatSnapsToWholeBarsAndToANeighbourEdgeWithinTwelvePixels) {
    TwoSections f;
    const juce::Point<float> from{f.x(2.0), 12.0f};
    f.strip.mouseDown(f.event(from, from, false));
    // Left edge at beat 21.3 (far from any block): the nearest bar is 20.
    f.strip.mouseDrag(f.event({f.x(21.3 + 2.0), 12.0f}, from, true));
    EXPECT_DOUBLE_EQ(f.strip.getBlockDragTargetBeatForTest(), 20.0);
    // Left edge 6 px (0.15 beat) past the Verse's end at 16: snaps to 16.
    f.strip.mouseDrag(f.event({f.x(16.15 + 2.0), 12.0f}, from, true));
    EXPECT_DOUBLE_EQ(f.strip.getBlockDragTargetBeatForTest(), 16.0);
    // Inside the Verse (8..16), nearer its end: goes to the end; nearer its start: the start.
    f.strip.mouseDrag(f.event({f.x(13.0 + 2.0), 12.0f}, from, true));
    EXPECT_DOUBLE_EQ(f.strip.getBlockDragTargetBeatForTest(), 16.0);
    f.strip.mouseDrag(f.event({f.x(9.0 + 2.0), 12.0f}, from, true));
    EXPECT_DOUBLE_EQ(f.strip.getBlockDragTargetBeatForTest(), 8.0);
    f.strip.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
}

TEST(TimelineSectionStripDragTest, OptionAtTheFirstDragEventMakesItACopyDrag) {
    TwoSections f;
    const juce::Point<float> from{f.x(2.0), 12.0f}, to{f.x(26.0), 12.0f};
    f.strip.mouseDown(f.event(from, from, false));
    f.strip.mouseDrag(f.event(to, from, true, kAlt));
    EXPECT_TRUE(f.strip.isBlockDragCopyForTest());
    f.strip.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
}

// What the drag showed (the accent drop line) is what the drop wrote, for a move and for a copy.
TEST(TimelineSectionStripDragTest, WhatTheDragPreviewsIsWhatTheDropCommits) {
    for (const bool copy : {false, true}) {
        TwoSections f;
        const juce::Point<float> from{f.x(2.0), 12.0f}, to{f.x(21.3 + 2.0), 12.0f};
        const int flags = copy ? kAlt : 0;
        f.strip.mouseDown(f.event(from, from, false, flags));
        f.strip.mouseDrag(f.event(to, from, true, flags));
        const double previewed = f.strip.getBlockDragTargetBeatForTest();
        f.strip.mouseUp(f.event(to, from, true, flags));
        const auto* placed = copy ? &f.doc.getPlacements().back() : f.doc.getPlacement(f.intro);
        ASSERT_NE(placed, nullptr);
        // A forward move's line is where the block goes in the timeline as it was; its own bars leave first.
        const double expected = copy || previewed < 0.0 + 1e-9 ? previewed : std::max(0.0, previewed - 8.0);
        EXPECT_DOUBLE_EQ(placed->startBeat, expected) << (copy ? "copy" : "move");
    }
    // The make drag shows the span it commits.
    TwoSections g;
    const juce::Point<float> start{g.x(21.0), 12.0f}, end{g.x(27.5), 12.0f};
    g.strip.mouseDown(g.event(start, start, false));
    g.strip.mouseDrag(g.event(end, start, true));
    const auto shown = g.strip.getMakeDragSpanForTest();
    g.strip.mouseUp(g.event(end, start, true));
    const auto& made = g.doc.getPlacements().back();
    EXPECT_DOUBLE_EQ(made.startBeat, shown.start);
    EXPECT_DOUBLE_EQ(g.doc.getSection(made.section)->lengthBeats, shown.length());
}
