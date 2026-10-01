// AutomationLanesSectionsBandTests.cpp -- an LFO modulator's sections against a real MainComponent, driven by
// real mouse and key events on the band under its modulator row: Draw, Erase and Select, double-click, the
// keyboard, and what each gesture writes (one undo step, one doc notification, exact breakpoints).

#include "AutomationLanesSectionsFixture.h"

using namespace sections_test;

namespace {
const juce::KeyPress kLeft(juce::KeyPress::leftKey);
const juce::KeyPress kRight(juce::KeyPress::rightKey);
const juce::KeyPress kDelete(juce::KeyPress::deleteKey);
const juce::KeyPress kReturn(juce::KeyPress::returnKey);
const juce::KeyPress kEscape(juce::KeyPress::escapeKey);
} // namespace

TEST_F(TimelinePanelIntegrationTest, DrawingOverBarsNineToSixteenCreatesTheLevelLaneInOneUndoStep) {
    SectionsScene s;
    ASSERT_NE(s.band(), nullptr);
    ASSERT_GT(s.band()->getWidth(), (int)s.x(bar(17))) << "the drawn bars fit the band";
    ASSERT_EQ(s.levelLane(), nullptr) << "no lane: the modulator is on everywhere";

    s.drawBars(9, 16);

    const auto* lane = s.levelLane();
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(s.doc().getTrackForLane(lane->id)->id, s.track) << "on the track of the lane it modulates";
    EXPECT_EQ(lane->nodeUuid, s.lfoUuid);
    EXPECT_EQ(pointsOf(lane), holdPoints({{0.0, 0.0}, {32.0, 1.0}, {64.0, 0.0}}));
    EXPECT_EQ(lane->recordMode, static_cast<int>(synth::LaneRecordMode::Read));

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.levelLane(), nullptr) << "one undo removes the lane";
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), 1u) << "and only that: the modulator stays";
    ASSERT_TRUE(s.undo().redo());
    EXPECT_EQ(pointsOf(s.levelLane()), holdPoints({{0.0, 0.0}, {32.0, 1.0}, {64.0, 0.0}}));
}

TEST_F(TimelinePanelIntegrationTest, AGestureWritesNothingUntilTheMouseIsReleaseAndThenOnceEachGestureIsOneStep) {
    SectionsScene s;
    s.drawBars(9, 16);
    NotificationCounter counter;
    s.doc().addListener(&counter);

    // A drag previews on the band alone: no doc write until the release.
    auto& band = *s.band();
    s.panel().setActiveTool(synth::ui::EditTool::Draw);
    band.mouseDown(makeClickEvent(band, {s.x(bar(20)), s.midY()}, leftButton()));
    band.mouseDrag(makeDragEvent(band, {s.x(bar(24)), s.midY()}, {s.x(bar(20)), s.midY()}, leftButton()));
    EXPECT_EQ(counter.count, 0);
    EXPECT_TRUE(band.isDragActive());
    EXPECT_EQ(s.blocks(), (SectionBlocks{{32.0, 64.0}})) << "the doc still holds the old sections";
    band.mouseUp(makeDragEvent(band, {s.x(bar(24)), s.midY()}, {s.x(bar(20)), s.midY()}, leftButton()));
    EXPECT_EQ(counter.count, 1) << "one mutation";
    EXPECT_EQ(s.blocks(), (SectionBlocks{{32.0, 64.0}, {76.0, 92.0}}));

    ASSERT_TRUE(s.undo().undo()) << "one undo step";
    EXPECT_EQ(s.blocks(), (SectionBlocks{{32.0, 64.0}}));
    s.doc().removeListener(&counter);
}

TEST_F(TimelinePanelIntegrationTest, ADrawnBlockOverlappingAnotherMergesWithIt) {
    SectionsScene s;
    s.drawBars(9, 16);
    s.drawBars(14, 20);
    EXPECT_EQ(s.blocks(), (SectionBlocks{{32.0, 80.0}}));
    EXPECT_EQ(pointsOf(s.levelLane()), holdPoints({{0.0, 0.0}, {32.0, 1.0}, {80.0, 0.0}}));

    s.drawBars(21, 22); // touching: bar 21 starts where bar 20 ends
    EXPECT_EQ(s.blocks(), (SectionBlocks{{32.0, 88.0}}));
    s.drag(bar(30), bar(30)); // a click with the Draw tool paints nothing
    EXPECT_EQ(s.blocks(), (SectionBlocks{{32.0, 88.0}}));
}

TEST_F(TimelinePanelIntegrationTest, ErasingSplitsAndShrinksBlocksAndTheLastErasedBlockRemovesTheLane) {
    SectionsScene s;
    s.drawBars(9, 16);
    s.panel().setActiveTool(synth::ui::EditTool::Erase);

    s.drag(bar(11), bar(13));
    EXPECT_EQ(s.blocks(), (SectionBlocks{{32.0, 40.0}, {48.0, 64.0}})) << "splits";
    s.drag(bar(7), bar(10));
    EXPECT_EQ(s.blocks(), (SectionBlocks{{36.0, 40.0}, {48.0, 64.0}})) << "shrinks";
    EXPECT_EQ(pointsOf(s.levelLane()), holdPoints({{0.0, 0.0}, {36.0, 1.0}, {40.0, 0.0}, {48.0, 1.0}, {64.0, 0.0}}));

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.blocks(), (SectionBlocks{{32.0, 40.0}, {48.0, 64.0}}));

    s.drag(bar(1), bar(20));
    EXPECT_EQ(s.levelLane(), nullptr) << "no sections left: back to on everywhere, the lane is gone";
    ASSERT_TRUE(s.undo().undo()) << "in one undo step";
    EXPECT_EQ(s.blocks(), (SectionBlocks{{32.0, 40.0}, {48.0, 64.0}}));
}

TEST_F(TimelinePanelIntegrationTest, ErasingOutOfAnEverywhereBandLeavesEverywhereButTheErasedSpan) {
    SectionsScene s(synth::ui::EditTool::Erase);
    ASSERT_EQ(s.levelLane(), nullptr);
    s.drag(bar(3), bar(5));
    ASSERT_NE(s.levelLane(), nullptr);
    EXPECT_EQ(s.blocks(), (SectionBlocks{{0.0, 8.0}, {16.0, synth::ui::kOpenEnd}}));
    EXPECT_EQ(pointsOf(s.levelLane()), holdPoints({{0.0, 1.0}, {8.0, 0.0}, {16.0, 1.0}}));
}

TEST_F(TimelinePanelIntegrationTest, SelectResizesFromEitherEdgeMovesTheMiddleAndClickSelects) {
    SectionsScene s;
    s.drawBars(9, 16);
    s.panel().setActiveTool(synth::ui::EditTool::Select);
    NotificationCounter counter;
    s.doc().addListener(&counter);

    s.click(bar(12));
    EXPECT_EQ(s.band()->getSelectedStart(), std::optional<double>(32.0));
    EXPECT_EQ(counter.count, 0) << "a click only selects";

    s.drag(bar(17) + 0.4, bar(19)); // the end edge, a few pixels off, to bar 19 (6 px zone = 0.75 beat)
    EXPECT_EQ(s.blocks(), (SectionBlocks{{32.0, 72.0}}));
    EXPECT_EQ(counter.count, 1);

    s.drag(bar(9) - 0.4, bar(7)); // the start edge
    EXPECT_EQ(s.blocks(), (SectionBlocks{{24.0, 72.0}}));
    EXPECT_EQ(s.band()->getSelectedStart(), std::optional<double>(24.0)) << "the resized block stays selected";

    s.drag(bar(12), bar(14)); // the middle: moves the whole block by two bars
    EXPECT_EQ(s.blocks(), (SectionBlocks{{32.0, 80.0}}));
    EXPECT_EQ(counter.count, 3) << "three gestures, three mutations";
    s.doc().removeListener(&counter);

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.blocks(), (SectionBlocks{{24.0, 72.0}})) << "each gesture is one undo step";
    s.click(bar(30)); // empty band
    EXPECT_FALSE(s.band()->getSelectedStart().has_value());
}

TEST_F(TimelinePanelIntegrationTest, AnEdgeNeverCrossesTheOtherAndAMoveStopsAtBeatZero) {
    SectionsScene s;
    s.drawBars(3, 4); // [8, 16)
    s.panel().setActiveTool(synth::ui::EditTool::Select);
    s.drag(bar(5), bar(1)); // the end edge dragged far past the start: one bar long at least
    EXPECT_EQ(s.blocks(), (SectionBlocks{{8.0, 12.0}}));
    s.drag(bar(4) - 1.0, bar(-5)); // the middle, far to the left
    EXPECT_EQ(s.blocks().front().start, 0.0);
    EXPECT_EQ(s.blocks().front().end, 4.0);
}

TEST_F(TimelinePanelIntegrationTest, DeleteRemovesTheSelectedBlockEscapeClearsAndErasingTheLastOneRemovesTheLane) {
    SectionsScene s;
    s.drawBars(3, 4);
    s.drawBars(9, 16);
    s.panel().setActiveTool(synth::ui::EditTool::Select);
    EXPECT_FALSE(s.key(kDelete)) << "nothing selected: the key falls through";

    s.click(bar(12));
    ASSERT_TRUE(s.band()->getSelectedStart().has_value());
    EXPECT_TRUE(s.key(kEscape));
    EXPECT_FALSE(s.band()->getSelectedStart().has_value());
    EXPECT_FALSE(s.key(kEscape)) << "idle: the key belongs to the panel";

    s.click(bar(12));
    EXPECT_TRUE(s.key(kDelete));
    EXPECT_EQ(s.blocks(), (SectionBlocks{{8.0, 16.0}}));
    s.click(bar(4));
    EXPECT_TRUE(s.key(juce::KeyPress(juce::KeyPress::backspaceKey)));
    EXPECT_EQ(s.levelLane(), nullptr) << "the last block takes the lane with it";
    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.blocks(), (SectionBlocks{{8.0, 16.0}}));
}

TEST_F(TimelinePanelIntegrationTest, DoubleClickOnEmptyBandAddsAOneBarBlockAtTheSnappedBeat) {
    SectionsScene s(synth::ui::EditTool::Select);
    s.doubleClick(bar(13) + 1.2);
    EXPECT_EQ(s.blocks(), (SectionBlocks{{48.0, 52.0}}));
    EXPECT_EQ(s.band()->getSelectedStart(), std::optional<double>(48.0));
    s.doubleClick(bar(13) + 2.0); // inside the block: nothing
    EXPECT_EQ(s.blocks(), (SectionBlocks{{48.0, 52.0}}));
    s.doubleClick(bar(14)); // the next bar: merges
    EXPECT_EQ(s.blocks(), (SectionBlocks{{48.0, 56.0}}));
    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.blocks(), (SectionBlocks{{48.0, 52.0}}));
}

TEST_F(TimelinePanelIntegrationTest, TheKeyboardWalksBlocksAndReturnAddsABarAtThePlayhead) {
    SectionsScene s;
    s.drawBars(3, 4);
    s.drawBars(9, 10);
    EXPECT_TRUE(s.band()->getWantsKeyboardFocus()) << "a Tab stop";

    EXPECT_TRUE(s.key(kRight));
    EXPECT_EQ(s.band()->getSelectedStart(), std::optional<double>(8.0));
    EXPECT_TRUE(s.key(kRight));
    EXPECT_EQ(s.band()->getSelectedStart(), std::optional<double>(32.0));
    EXPECT_TRUE(s.key(kRight));
    EXPECT_EQ(s.band()->getSelectedStart(), std::optional<double>(32.0)) << "stays on the last";
    EXPECT_TRUE(s.key(kLeft));
    EXPECT_EQ(s.band()->getSelectedStart(), std::optional<double>(8.0));

    // The playhead stands at the start: Return adds the bar there.
    EXPECT_TRUE(s.key(kReturn));
    EXPECT_EQ(s.blocks(), (SectionBlocks{{0.0, 4.0}, {8.0, 16.0}, {32.0, 40.0}}));
    EXPECT_TRUE(s.key(kDelete)) << "and the new block is the selected one";
    EXPECT_EQ(s.blocks(), (SectionBlocks{{8.0, 16.0}, {32.0, 40.0}}));
}

TEST_F(TimelinePanelIntegrationTest, TheBandIsNamedDescribedAndTippedForTheScreenReaderAndTheMouse) {
    SectionsScene s;
    auto& band = *s.band();
    EXPECT_EQ(band.getTitle(), s.row()->getTitle() + " sections");
    EXPECT_TRUE(band.getTitle().endsWith("LFO sections"));
    EXPECT_EQ(band.getDescription(), "on everywhere");
    EXPECT_TRUE(band.getTooltip().contains("Draw sections to play this modulator only there"));
    EXPECT_TRUE(band.getTooltip().contains("Draw tool"));
    EXPECT_TRUE(band.getTooltip().contains("erasing the last one goes back"));

    s.drawBars(9, 16);
    s.drawBars(25, 32);
    const auto dash = juce::String::fromUTF8("\xE2\x80\x93");
    EXPECT_EQ(band.getDescription(), "bars 9" + dash + "16, 25" + dash + "32");
    EXPECT_TRUE(band.getTooltip().startsWith("Sections: bars 9"));
}

TEST_F(TimelinePanelIntegrationTest, AnotherSourcesBandIsAPlainDecorationThatTakesNoClicks) {
    SectionsScene s;
    synth::ui::ModulatorInfo info = s.row()->getInfo();
    info.isLfo = false;
    info.sourceUuid = "some-envelope";
    auto& band = *s.band();
    band.setModulator(info, s.lane, "Cutoff");
    EXPECT_FALSE(band.isEditable());
    bool allowsClicks = true, allowsChildClicks = true;
    band.getInterceptsMouseClicks(allowsClicks, allowsChildClicks);
    EXPECT_FALSE(allowsClicks);
    EXPECT_FALSE(band.getWantsKeyboardFocus());
    EXPECT_FALSE(band.isAccessible());
    EXPECT_TRUE(band.getTooltip().isEmpty());
    s.drag(bar(3), bar(5));
    EXPECT_EQ(s.doc().getLaneForParam("some-envelope", "level"), nullptr);
}
