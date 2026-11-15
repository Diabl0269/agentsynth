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
    f.dragBlock(0.0, 8.0); // the left edge at the Verse's end once the Intro's gap closes: the Intro goes after it
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
    // Cover (Shift held): the Verse stays where it is while the Intro is dragged.
    // Left edge at beat 21.3 (far from any block): the nearest bar is 20.
    f.strip.mouseDrag(f.event({f.x(21.3 + 2.0), 12.0f}, from, true, kShift));
    EXPECT_DOUBLE_EQ(f.strip.getBlockDragTargetBeatForTest(), 20.0);
    // Left edge 6 px (0.15 beat) past the Verse's end at 16: snaps to 16.
    f.strip.mouseDrag(f.event({f.x(16.15 + 2.0), 12.0f}, from, true, kShift));
    EXPECT_DOUBLE_EQ(f.strip.getBlockDragTargetBeatForTest(), 16.0);
    // Inside the Verse (8..16), nearer its end: goes to the end; nearer its start: the start.
    f.strip.mouseDrag(f.event({f.x(13.0 + 2.0), 12.0f}, from, true, kShift));
    EXPECT_DOUBLE_EQ(f.strip.getBlockDragTargetBeatForTest(), 16.0);
    f.strip.mouseDrag(f.event({f.x(9.0 + 2.0), 12.0f}, from, true, kShift));
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
        EXPECT_DOUBLE_EQ(placed->startBeat, previewed) << (copy ? "copy" : "move");
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

// ---- Free span: make room only pushes when the landing span is taken ----

namespace {

constexpr int kCmd = juce::ModifierKeys::commandModifier;

// Section 1 over bars 5-6 (16..24) with bars 1-4 free before it; Section 2 over 32..36 with a gap between them.
struct GapSong : StripFixture {
    PlacementId one, two;
    GapSong() {
        strip.setSize(2000, 24); // wide enough to hold Section 2 (beat 32 and on) and the pointer past it
        doc.addClip(track, 16.0, 4.0, "One clip");
        doc.addClip(track, 32.0, 2.0, "Two clip");
        doc.makeSection(16.0, 24.0, "Section 1", 0xff56D4C0, 0.5);
        doc.makeSection(32.0, 36.0, "Section 2", 0xff4FC1FF, 0.5);
        strip.refresh();
        one = doc.getPlacements()[0].id;
        two = doc.getPlacements()[1].id;
    }
    double startOf(PlacementId id) const { return doc.getPlacement(id)->startBeat; }
    std::vector<double> starts() const {
        std::vector<double> out;
        for (const auto& p : doc.getPlacements())
            out.push_back(p.startBeat);
        return out;
    }
};

} // namespace

TEST(TimelineSectionStripFreeGapTest, OptionDragIntoTheFreeBarsBeforeTheOriginalPushesNothing) {
    GapSong f;
    const juce::Point<float> from{f.x(18.0), 12.0f}, to{f.x(2.0), 12.0f}; // grabbed 2 beats in; the copy's edge at 0
    f.strip.mouseDown(f.event(from, from, false, kAlt));
    f.strip.mouseDrag(f.event(to, from, true, kAlt));
    EXPECT_FALSE(f.strip.getBlockDragPlanForTest().opensRoom);
    EXPECT_EQ(f.strip.getDropHintTextForTest(), "Release to place a copy here");
    f.strip.mouseUp(f.event(to, from, true, kAlt));
    ASSERT_EQ(f.doc.getPlacements().size(), 3u);
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[0].startBeat, 0.0) << "the copy at bar 1";
    EXPECT_DOUBLE_EQ(f.startOf(f.one), 16.0) << "Section 1 stays at bar 5";
    EXPECT_DOUBLE_EQ(f.startOf(f.two), 32.0);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getPlacements().size(), 2u) << "one undo step takes the copy away";
    EXPECT_DOUBLE_EQ(f.startOf(f.one), 16.0);
    EXPECT_FALSE(f.undo.undo());
}

TEST(TimelineSectionStripFreeGapTest, AnOccupiedSpotStillMakesRoomAndSaysInsert) {
    GapSong f;
    const juce::Point<float> from{f.x(18.0), 12.0f}, to{f.x(30.0), 12.0f}; // the copy's span 28..36 overlaps Section 2
    f.strip.mouseDown(f.event(from, from, false, kAlt));
    f.strip.mouseDrag(f.event(to, from, true, kAlt));
    EXPECT_TRUE(f.strip.getBlockDragPlanForTest().opensRoom);
    EXPECT_EQ(f.strip.getDropHintTextForTest(), "Release to insert a copy here");
    f.strip.mouseUp(f.event(to, from, true, kAlt));
    EXPECT_GT(f.startOf(f.two), 32.0) << "Section 2 moved later to make room";
}

// The doc after a drop equals the plan the drag showed, for every spot, copy or move, in make-room mode.
TEST(TimelineSectionStripFreeGapTest, TheDocAfterTheDropEqualsThePlanOverEverySpot) {
    for (const bool copy : {false, true})
        for (const int pressed : {0, 1})
            for (double toBeat = 0.0; toBeat <= 44.0; toBeat += 2.0) {
                GapSong f;
                const double blockStart = pressed == 0 ? 16.0 : 32.0;
                if (toBeat == blockStart)
                    continue; // no pointer travel: not a drag
                const juce::Point<float> from{f.x(blockStart + 1.0), 12.0f}, to{f.x(toBeat + 1.0), 12.0f};
                const int flags = copy ? kAlt : 0;
                f.strip.mouseDown(f.event(from, from, false, flags));
                f.strip.mouseDrag(f.event(to, from, true, flags));
                const auto plan = f.strip.getBlockDragPlanForTest();
                const auto before = f.doc.getPlacements();
                f.strip.mouseUp(f.event(to, from, true, flags));
                const auto label = juce::String("copy ") + juce::String((int)copy) + " pressed " +
                                   juce::String(pressed) + " to " + juce::String(toBeat);
                for (const auto& other : plan.others) {
                    const auto* now = f.doc.getPlacement(PlacementId{other.id});
                    ASSERT_NE(now, nullptr) << label;
                    EXPECT_DOUBLE_EQ(now->startBeat, other.start) << label;
                }
                for (const auto& [id, start] : plan.group) {
                    if (copy)
                        continue; // the copy has a new id: checked below
                    EXPECT_DOUBLE_EQ(f.doc.getPlacement(PlacementId{id})->startBeat, start) << label;
                }
                if (copy) {
                    ASSERT_EQ(f.doc.getPlacements().size(), before.size() + 1) << label;
                    bool found = false;
                    for (const auto& p : f.doc.getPlacements())
                        found |=
                            std::abs(p.startBeat - plan.landing) < 1e-9 &&
                            std::none_of(before.begin(), before.end(), [&](const auto& b) { return b.id == p.id; });
                    EXPECT_TRUE(found) << label << ": the copy is at the landing";
                }
            }
}

// Cmd+D places the section again after the block: into the gap there, nothing moves; without room, what follows moves.
TEST(TimelineSectionStripFreeGapTest, CmdDPlacesIntoAFreeGapAndPushesOnlyWithoutRoom) {
    GapSong f;
    f.strip.selectPlacement(f.one);
    ASSERT_TRUE(f.strip.keyPressed(juce::KeyPress('d', juce::ModifierKeys(kCmd), 0)));
    EXPECT_DOUBLE_EQ(f.startOf(f.two), 32.0) << "the 8-beat gap after Section 1 holds the copy";
    EXPECT_EQ(f.doc.getPlacements().size(), 3u);
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[1].startBeat, 24.0);
    f.strip.selectPlacement(f.one);
    ASSERT_TRUE(f.strip.keyPressed(juce::KeyPress('d', juce::ModifierKeys(kCmd), 0)));
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[1].startBeat, 24.0) << "a copy is placed after Section 1 again, inserted";
    EXPECT_GT(f.startOf(f.two), 32.0) << "the gap is full now, so Section 2 moves later";
}

// Option+Left/Right swap a block with its neighbour in the song order: the same plan as the drag, so a free span after
// the move leaves the blocks it does not cross where they are.
TEST(TimelineSectionStripFreeGapTest, OptionArrowsFollowThePlan) {
    GapSong f;
    f.strip.selectPlacement(f.one);
    ASSERT_TRUE(f.strip.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(kAlt), 0)));
    // Section 1's bars close up (Section 2 to 24) and it lands right after Section 2 (28..36), a free span: the plan
    // pushes nothing.
    EXPECT_DOUBLE_EQ(f.startOf(f.two), 24.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.one), 28.0);
    EXPECT_TRUE(f.undo.undo());
    EXPECT_DOUBLE_EQ(f.startOf(f.one), 16.0);
    EXPECT_DOUBLE_EQ(f.startOf(f.two), 32.0);
}

// Music loose between blocks is never covered: a copy that would land on it makes room instead of stacking on it.
TEST(TimelineSectionStripFreeGapTest, ALooseClipInTheSpanStillMakesRoom) {
    GapSong f;
    f.doc.addClip(f.track, 26.0, 2.0, "Loose");
    f.strip.refresh();
    f.strip.selectPlacement(f.one);
    ASSERT_TRUE(f.strip.keyPressed(juce::KeyPress('d', juce::ModifierKeys(kCmd), 0)));
    EXPECT_DOUBLE_EQ(f.doc.getPlacements()[1].startBeat, 24.0);
    EXPECT_GT(f.startOf(f.two), 32.0) << "the copy at 24..32 met the loose clip, so what follows moved";
}
