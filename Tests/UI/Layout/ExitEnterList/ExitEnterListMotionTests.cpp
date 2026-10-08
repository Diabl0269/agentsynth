// ExitEnterListMotionTests.cpp: the plan that cuts a list's picture into slices and the overlay that plays it
// (Source/UI/Layout/ExitEnterList/). Headless: the timeline is stepped by hand with applyAtMs().
#include "UI/Layout/ExitEnterList/ExitEnterListMotion.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

using synth::ui::ExitEnterListItem;
using synth::ui::ExitEnterListMotion;
using synth::ui::ExitEnterListPlan;
using synth::ui::ExitEnterListRow;
using synth::ui::ExitEnterTimeline;
using synth::ui::ListAxis;
using Role = ExitEnterListItem::Role;

namespace {

// Three rows of 40 down a 200-long picture: A 0..40, B 40..80, C 80..120.
std::vector<ExitEnterListRow> threeRows() { return {{"A", 0, 40}, {"B", 40, 40}, {"C", 80, 40}}; }

const ExitEnterListItem* itemFor(const std::vector<ExitEnterListItem>& items, const juce::String& key) {
    for (const auto& i : items)
        if (i.key == key)
            return &i;
    return nullptr;
}

struct ReducedGuard {
    explicit ReducedGuard(bool on) { synth::ui::setReducedMotionForTest(on); }
    ~ReducedGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

ExitEnterListMotion::Job jobFor(std::vector<ExitEnterListItem> items, ListAxis axis, int length) {
    ExitEnterListMotion::Job job;
    job.axis = axis;
    job.area = axis == ListAxis::Vertical ? juce::Rectangle<int>(10, 20, 100, length)
                                          : juce::Rectangle<int>(10, 20, length, 100);
    job.picture = juce::Image(juce::Image::ARGB, job.area.getWidth(), job.area.getHeight(), true);
    job.background = juce::Colours::black;
    job.accent = juce::Colours::cyan;
    job.items = std::move(items);
    return job;
}

} // namespace

TEST(ExitEnterListPlan, RemovalExitsTheRowAndSlidesTheOnesAfterIt) {
    const auto before = threeRows();
    const std::vector<ExitEnterListRow> after{{"A", 0, 40}, {"C", 40, 40}};
    const auto items = ExitEnterListPlan::forRemoval(before, after, 200.0f);

    ASSERT_EQ(items.size(), 4u) << "three rows and the tail";
    EXPECT_EQ(itemFor(items, "A")->role, Role::Stay);
    EXPECT_EQ(itemFor(items, "B")->role, Role::Exit);
    const auto* c = itemFor(items, "C");
    EXPECT_EQ(c->role, Role::Stay);
    EXPECT_FLOAT_EQ(c->fromStart, 80.0f) << "C waits at its old place";
    EXPECT_FLOAT_EQ(c->toStart, 40.0f);
    const auto* tail = itemFor(items, "");
    EXPECT_FLOAT_EQ(tail->srcStart, 120.0f);
    EXPECT_FLOAT_EQ(tail->toStart, 80.0f) << "the empty space below follows the rows up";

    const auto timeline = ExitEnterListPlan::timelineFor(items);
    EXPECT_TRUE(timeline.hasExit);
    EXPECT_TRUE(timeline.hasGap);
    EXPECT_FALSE(timeline.hasEnter);
}

TEST(ExitEnterListPlan, RemovingTheLastRowNeedsNoGap) {
    const auto items = ExitEnterListPlan::forRemoval(threeRows(), {{"A", 0, 40}, {"B", 40, 40}}, 200.0f);
    const auto timeline = ExitEnterListPlan::timelineFor(items);
    EXPECT_TRUE(timeline.hasExit);
    EXPECT_FALSE(timeline.hasGap) << "nothing is below it to close up";
}

TEST(ExitEnterListPlan, InsertionStartsTheRowsAfterTheNewOneWhereTheyStoodWithoutIt) {
    const auto items = ExitEnterListPlan::forInsertion(threeRows(), {"B"}, 200.0f);
    EXPECT_EQ(itemFor(items, "B")->role, Role::Enter);
    EXPECT_FLOAT_EQ(itemFor(items, "A")->fromStart, 0.0f);
    EXPECT_FLOAT_EQ(itemFor(items, "C")->fromStart, 40.0f) << "C stood where B is now";
    EXPECT_FLOAT_EQ(itemFor(items, "C")->toStart, 80.0f);

    const auto timeline = ExitEnterListPlan::timelineFor(items);
    EXPECT_TRUE(timeline.hasEnter);
    EXPECT_TRUE(timeline.hasGap);
    EXPECT_FALSE(timeline.hasExit);
}

TEST(ExitEnterListPlan, ARemovedSliceShrinksThenGoesAndTheOnesAfterItWaitForTheExit) {
    const auto items = ExitEnterListPlan::forRemoval(threeRows(), {{"A", 0, 40}, {"C", 40, 40}}, 200.0f);
    const auto timeline = ExitEnterListPlan::timelineFor(items);
    const auto& b = *itemFor(items, "B");
    const auto& c = *itemFor(items, "C");

    const auto half = ExitEnterListPlan::drawnAt(b, timeline.at(ExitEnterTimeline::kExitMs * 0.5), false);
    ASSERT_TRUE(half.has_value());
    EXPECT_LT(half->scale, 1.0f);
    EXPECT_GT(half->scale, 0.0f);
    EXPECT_FLOAT_EQ(ExitEnterListPlan::drawnAt(c, timeline.at(ExitEnterTimeline::kExitMs), false)->start, 80.0f)
        << "no row moves while one is still shrinking";
    EXPECT_FALSE(ExitEnterListPlan::drawnAt(b, timeline.at(ExitEnterTimeline::kExitMs), false).has_value());
    EXPECT_FLOAT_EQ(
        ExitEnterListPlan::drawnAt(c, timeline.at(ExitEnterTimeline::kExitMs + ExitEnterTimeline::kGapMs), false)
            ->start,
        40.0f);
}

TEST(ExitEnterListPlan, ARestoredSliceWaitsForTheGapThenGrowsThenIsOutlined) {
    const auto items = ExitEnterListPlan::forInsertion(threeRows(), {"B"}, 200.0f);
    const auto timeline = ExitEnterListPlan::timelineFor(items);
    const auto& b = *itemFor(items, "B");

    EXPECT_FALSE(ExitEnterListPlan::drawnAt(b, timeline.at(ExitEnterTimeline::kGapMs), false).has_value())
        << "the gap is open and empty before it grows";
    const auto growing = ExitEnterListPlan::drawnAt(b, timeline.at(ExitEnterTimeline::kGapMs + 90.0), false);
    ASSERT_TRUE(growing.has_value());
    EXPECT_GT(growing->scale, 0.0f);
    EXPECT_LT(growing->scale, 1.0f);
    EXPECT_FLOAT_EQ(growing->outlineAlpha, 0.0f);

    const double grown = ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs;
    const auto outlined = ExitEnterListPlan::drawnAt(b, timeline.at(grown + 100.0), false);
    ASSERT_TRUE(outlined.has_value());
    EXPECT_FLOAT_EQ(outlined->scale, 1.0f);
    EXPECT_GT(outlined->outlineAlpha, 0.0f);
    EXPECT_FLOAT_EQ(ExitEnterListPlan::drawnAt(b, timeline.at(timeline.totalMs()), false)->outlineAlpha, 0.0f);
}

TEST(ExitEnterListPlan, ReduceMotionFadesInPlaceInsteadOfScaling) {
    const auto items = ExitEnterListPlan::forRemoval(threeRows(), {{"A", 0, 40}, {"C", 40, 40}}, 200.0f);
    const auto timeline = ExitEnterListPlan::timelineFor(items);
    const auto d =
        ExitEnterListPlan::drawnAt(*itemFor(items, "B"), timeline.at(ExitEnterTimeline::kExitMs * 0.5), true);
    ASSERT_TRUE(d.has_value());
    EXPECT_FLOAT_EQ(d->scale, 1.0f);
    EXPECT_LT(d->alpha, 1.0f);
}

TEST(ExitEnterListMotion, TheOverlayCoversTheListIsInertAndGoesWhenTheTimelineEnds) {
    ReducedGuard guard(false);
    juce::Component host;
    host.setSize(300, 300);
    ExitEnterListMotion motion(host);
    const auto items = ExitEnterListPlan::forRemoval(threeRows(), {{"A", 0, 40}, {"C", 40, 40}}, 200.0f);
    ASSERT_TRUE(motion.start(jobFor(items, ListAxis::Vertical, 200)));

    auto* overlay = motion.overlayComponent();
    ASSERT_NE(overlay, nullptr);
    EXPECT_EQ(overlay->getBounds(), juce::Rectangle<int>(10, 20, 100, 200));
    bool onThis = true, onChildren = true;
    overlay->getInterceptsMouseClicks(onThis, onChildren);
    EXPECT_FALSE(onThis || onChildren) << "the real row underneath still gets the mouse";
    EXPECT_FALSE(overlay->getWantsKeyboardFocus());
    EXPECT_FALSE(overlay->isAccessible()) << "a picture is not a control";

    EXPECT_EQ(motion.exitGhostCount(), 1);
    ASSERT_TRUE(motion.drawnRectFor("B").has_value());
    EXPECT_FLOAT_EQ(motion.drawnRectFor("C")->getY(), 20.0f + 80.0f);

    motion.applyAtMs(ExitEnterTimeline::kExitMs);
    EXPECT_EQ(motion.exitGhostCount(), 0);
    EXPECT_FALSE(motion.drawnRectFor("B").has_value());
    EXPECT_FLOAT_EQ(motion.drawnRectFor("C")->getY(), 20.0f + 80.0f);

    motion.applyAtMs(motion.timeline().totalMs());
    EXPECT_FLOAT_EQ(motion.drawnRectFor("C")->getY(), 20.0f + 40.0f);
    motion.finishNow();
    EXPECT_FALSE(motion.isRunning());
    EXPECT_EQ(host.getNumChildComponents(), 0);
}

TEST(ExitEnterListMotion, AHorizontalListMovesAlongX) {
    ReducedGuard guard(false);
    juce::Component host;
    host.setSize(400, 300);
    ExitEnterListMotion motion(host);
    const auto items = ExitEnterListPlan::forInsertion(threeRows(), {"B"}, 200.0f);
    ASSERT_TRUE(motion.start(jobFor(items, ListAxis::Horizontal, 200)));

    EXPECT_FLOAT_EQ(motion.drawnRectFor("C")->getX(), 10.0f + 40.0f) << "C starts where B now stands";
    motion.applyAtMs(ExitEnterTimeline::kGapMs);
    EXPECT_FLOAT_EQ(motion.drawnRectFor("C")->getX(), 10.0f + 80.0f);
    EXPECT_FALSE(motion.drawnRectFor("B").has_value());
    motion.applyAtMs(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs);
    EXPECT_EQ(motion.enterGhostCount(), 1);
    EXPECT_FLOAT_EQ(motion.outlineAlphaFor("B"), 1.0f) << "the outline starts at full strength";
    motion.applyAtMs(ExitEnterTimeline::kGapMs + ExitEnterTimeline::kGrowMs + 100.0);
    EXPECT_GT(motion.outlineAlphaFor("B"), 0.0f);
    EXPECT_LT(motion.outlineAlphaFor("B"), 1.0f);
}

TEST(ExitEnterListMotion, ReduceMotionKeepsTheSliceFullSizeAndFadesIt) {
    ReducedGuard guard(true);
    juce::Component host;
    host.setSize(300, 300);
    ExitEnterListMotion motion(host);
    const auto items = ExitEnterListPlan::forRemoval(threeRows(), {{"A", 0, 40}, {"C", 40, 40}}, 200.0f);
    ASSERT_TRUE(motion.start(jobFor(items, ListAxis::Vertical, 200)));
    EXPECT_TRUE(motion.isReducedMotion());
    motion.applyAtMs(ExitEnterTimeline::kExitMs * 0.5);
    const auto rect = motion.drawnRectFor("B");
    ASSERT_TRUE(rect.has_value());
    EXPECT_FLOAT_EQ(rect->getHeight(), 40.0f) << "a fade, not a shrink";
}

TEST(ExitEnterListMotion, NothingToShowStartsNothing) {
    juce::Component host;
    host.setSize(300, 300);
    ExitEnterListMotion motion(host);
    EXPECT_FALSE(motion.start(jobFor({}, ListAxis::Vertical, 200)));
    EXPECT_FALSE(motion.isRunning());
}
