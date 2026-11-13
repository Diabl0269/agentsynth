// ExitEnterListFromSourceTests.cpp: the plan variant for a duplicate -- the copy's slice starts on its original and
// travels to its slot while the slices after it make room, all in one movement; undo plays it backwards
// (Source/UI/Layout/ExitEnterList/). Headless: the timeline is stepped by hand with applyAtMs(). Positions are
// asserted at several moments, not only at the end (bug kind: motion-over-time).
#include "UI/Layout/ExitEnterList/ExitEnterListMotion.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

using synth::ui::ExitEnterListItem;
using synth::ui::ExitEnterListMotion;
using synth::ui::ExitEnterListPlan;
using synth::ui::ExitEnterListRow;
using synth::ui::ExitEnterTimeline;
using synth::ui::ListAxis;

namespace {

constexpr float kHostY = 20.0f; // the overlay sits at y = 20 in the host

// A 0..40, its copy Ac 40..80, B 80..120 (the list after a duplicate of A).
std::vector<ExitEnterListRow> afterDuplicate() { return {{"A", 0, 40}, {"Ac", 40, 40}, {"B", 80, 40}}; }
std::vector<ExitEnterListRow> withoutCopy() { return {{"A", 0, 40}, {"B", 40, 40}}; }

struct ReducedGuard {
    explicit ReducedGuard(bool on) { synth::ui::setReducedMotionForTest(on); }
    ~ReducedGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

ExitEnterListMotion::Job jobFor(std::vector<ExitEnterListItem> items, ListAxis axis = ListAxis::Vertical) {
    ExitEnterListMotion::Job job;
    job.axis = axis;
    job.area =
        axis == ListAxis::Vertical ? juce::Rectangle<int>(10, 20, 100, 200) : juce::Rectangle<int>(10, 20, 200, 100);
    job.picture = juce::Image(juce::Image::ARGB, job.area.getWidth(), job.area.getHeight(), true);
    job.background = juce::Colours::black;
    job.accent = juce::Colours::cyan;
    job.items = std::move(items);
    return job;
}

std::vector<ExitEnterListItem> forwardItems() {
    std::vector<ExitEnterListItem> items;
    EXPECT_TRUE(ExitEnterListPlan::forInsertionFromSource(afterDuplicate(), {"Ac"}, {{"Ac", "A"}}, 200.0f, items));
    return items;
}

const ExitEnterListItem* itemFor(const std::vector<ExitEnterListItem>& items, const juce::String& key) {
    for (const auto& i : items)
        if (i.key == key)
            return &i;
    return nullptr;
}

} // namespace

TEST(ExitEnterListFromSource, TheCopyStartsAtItsOriginalAndTheRowsBelowStartWhereTheyStoodWithoutIt) {
    const auto items = forwardItems();
    const auto* copy = itemFor(items, "Ac");
    ASSERT_NE(copy, nullptr);
    EXPECT_TRUE(copy->travels);
    EXPECT_FLOAT_EQ(copy->originStart, 0.0f) << "A's place";
    EXPECT_FLOAT_EQ(copy->srcStart, 40.0f) << "its slot";
    EXPECT_FLOAT_EQ(itemFor(items, "B")->fromStart, 40.0f);

    const auto timeline = ExitEnterListPlan::timelineFor(items);
    EXPECT_TRUE(timeline.together);
    EXPECT_FALSE(timeline.reverse);
    EXPECT_DOUBLE_EQ(timeline.totalMs(), ExitEnterTimeline::kOutOfSourceMs);
    EXPECT_FALSE(timeline.outline);
}

TEST(ExitEnterListFromSource, ACopyWithoutASourceFallsBackToThePlainGrowIn) {
    std::vector<ExitEnterListItem> items;
    EXPECT_FALSE(ExitEnterListPlan::forInsertionFromSource(afterDuplicate(), {"Ac"}, {}, 200.0f, items));
    EXPECT_FALSE(ExitEnterListPlan::forInsertionFromSource(afterDuplicate(), {"Ac"}, {{"Ac", "gone"}}, 200.0f, items));
    EXPECT_TRUE(items.empty());
}

TEST(ExitEnterListFromSource, TheCopyAndTheRowsBelowMoveInTheSameSpanAtEveryMoment) {
    ReducedGuard guard(false);
    juce::Component host;
    host.setSize(300, 300);
    ExitEnterListMotion motion(host);
    ASSERT_TRUE(motion.start(jobFor(forwardItems())));
    ASSERT_TRUE(motion.timeline().together);

    // t = 0: the copy sits on A (smaller, lifted), B has not moved.
    motion.applyAtMs(0.0);
    auto copy = motion.drawnRectFor("Ac");
    ASSERT_TRUE(copy.has_value());
    EXPECT_FLOAT_EQ(copy->getCentreY(), kHostY + 20.0f) << "the centre of A's row";
    EXPECT_LT(copy->getHeight(), 40.0f) << "it grows out of the original";
    EXPECT_FLOAT_EQ(motion.drawnRectFor("B")->getY(), kHostY + 40.0f);

    for (const double fraction : {0.25, 0.5, 0.75}) {
        motion.applyAtMs(ExitEnterTimeline::kOutOfSourceMs * fraction);
        const float eased = 1.0f - std::pow(1.0f - static_cast<float>(fraction), 3.0f);
        copy = motion.drawnRectFor("Ac");
        ASSERT_TRUE(copy.has_value()) << fraction;
        EXPECT_NEAR(copy->getCentreY(), kHostY + 20.0f + 40.0f * eased, 0.01f) << "travels from A to its slot";
        EXPECT_NEAR(motion.drawnRectFor("B")->getY(), kHostY + 40.0f + 40.0f * eased, 0.01f)
            << "B makes room in the same movement, at the same progress";
        EXPECT_GT(copy->getHeight(), 32.0f - 0.01f);
        EXPECT_LT(copy->getHeight(), 40.0f);
    }

    motion.applyAtMs(ExitEnterTimeline::kOutOfSourceMs);
    copy = motion.drawnRectFor("Ac");
    EXPECT_FLOAT_EQ(copy->getY(), kHostY + 40.0f);
    EXPECT_FLOAT_EQ(copy->getHeight(), 40.0f) << "a normal row on landing";
    EXPECT_FLOAT_EQ(motion.drawnRectFor("B")->getY(), kHostY + 80.0f);
    EXPECT_FLOAT_EQ(motion.outlineAlphaFor("Ac"), 0.0f) << "no outline: it is new, not brought back";
}

TEST(ExitEnterListFromSource, TheCopyIsLiftedWhileItMovesAndPlainOnLanding) {
    const auto items = forwardItems();
    const auto* copy = itemFor(items, "Ac");
    const auto timeline = ExitEnterListPlan::timelineFor(items);
    const auto at = [&](double ms) { return *ExitEnterListPlan::drawnAt(*copy, timeline.at(ms), false); };
    EXPECT_FLOAT_EQ(at(0.0).lift, 1.0f);
    EXPECT_GT(at(100.0).lift, 0.0f);
    EXPECT_LT(at(100.0).lift, 1.0f);
    EXPECT_FLOAT_EQ(at(ExitEnterTimeline::kOutOfSourceMs).lift, 0.0f);
    EXPECT_FLOAT_EQ(at(ExitEnterTimeline::kOutOfSourceMs).scale, 1.0f);
    EXPECT_LT(at(0.0).alpha, 1.0f) << "slightly see-through while it moves";
}

TEST(ExitEnterListFromSource, UndoSendsTheCopyBackIntoItsOriginalWhileTheRowsCloseUp) {
    ReducedGuard guard(false);
    std::vector<ExitEnterListItem> items;
    ASSERT_TRUE(ExitEnterListPlan::forRemovalIntoSource(afterDuplicate(), withoutCopy(), {{"Ac", "A"}}, 200.0f, items));
    EXPECT_TRUE(ExitEnterListPlan::timelineFor(items).reverse);
    EXPECT_DOUBLE_EQ(ExitEnterListPlan::timelineFor(items).totalMs(), ExitEnterTimeline::kIntoSourceMs);

    juce::Component host;
    host.setSize(300, 300);
    ExitEnterListMotion motion(host);
    ASSERT_TRUE(motion.start(jobFor(items)));

    motion.applyAtMs(0.0);
    EXPECT_FLOAT_EQ(motion.drawnRectFor("Ac")->getY(), kHostY + 40.0f) << "starts in its slot, full size";
    EXPECT_FLOAT_EQ(motion.drawnRectFor("B")->getY(), kHostY + 80.0f);

    for (const double fraction : {0.25, 0.5, 0.75}) {
        motion.applyAtMs(ExitEnterTimeline::kIntoSourceMs * fraction);
        const float eased = static_cast<float>(fraction * fraction * fraction); // ease in
        EXPECT_NEAR(motion.drawnRectFor("Ac")->getCentreY(), kHostY + 60.0f - 40.0f * eased, 0.01f);
        EXPECT_NEAR(motion.drawnRectFor("B")->getY(), kHostY + 80.0f - 40.0f * eased, 0.01f)
            << "the rows close up in the same movement";
    }

    motion.applyAtMs(ExitEnterTimeline::kIntoSourceMs);
    EXPECT_FLOAT_EQ(motion.drawnRectFor("B")->getY(), kHostY + 40.0f);
    EXPECT_FLOAT_EQ(motion.drawnRectFor("Ac")->getCentreY(), kHostY + 20.0f) << "it ends on A";
}

TEST(ExitEnterListFromSource, ARemovalWithoutASourceIsThePlainShrink) {
    std::vector<ExitEnterListItem> items;
    EXPECT_FALSE(ExitEnterListPlan::forRemovalIntoSource(afterDuplicate(), withoutCopy(), {}, 200.0f, items));
    EXPECT_TRUE(items.empty());
}

TEST(ExitEnterListFromSource, ReduceMotionPutsTheRowsInPlaceAtOnceAndFadesTheCopyOver80Ms) {
    ReducedGuard guard(true);
    juce::Component host;
    host.setSize(300, 300);
    ExitEnterListMotion motion(host);
    ASSERT_TRUE(motion.start(jobFor(forwardItems())));
    EXPECT_DOUBLE_EQ(motion.timeline().totalMs(), ExitEnterTimeline::kTogetherFadeMs);

    motion.applyAtMs(0.0);
    EXPECT_FLOAT_EQ(motion.drawnRectFor("B")->getY(), kHostY + 80.0f) << "nothing slides";
    EXPECT_FALSE(motion.drawnRectFor("Ac").has_value()) << "the copy is still fully transparent";
    motion.applyAtMs(ExitEnterTimeline::kTogetherFadeMs * 0.5);
    const auto copy = motion.drawnRectFor("Ac");
    ASSERT_TRUE(copy.has_value());
    EXPECT_FLOAT_EQ(copy->getY(), kHostY + 40.0f) << "in its slot, never travelling";
    EXPECT_FLOAT_EQ(copy->getHeight(), 40.0f) << "full size: a fade, not a grow";
}

TEST(ExitEnterListFromSource, SeveralCopiesEachComeOutOfTheirOwnOriginal) {
    ReducedGuard guard(false);
    const std::vector<ExitEnterListRow> after{
        {"A", 0, 40}, {"B", 40, 40}, {"Ac", 80, 40}, {"Bc", 120, 40}, {"C", 160, 40}};
    std::vector<ExitEnterListItem> items;
    ASSERT_TRUE(
        ExitEnterListPlan::forInsertionFromSource(after, {"Ac", "Bc"}, {{"Ac", "A"}, {"Bc", "B"}}, 200.0f, items));
    EXPECT_FLOAT_EQ(itemFor(items, "Ac")->originStart, 0.0f);
    EXPECT_FLOAT_EQ(itemFor(items, "Bc")->originStart, 40.0f);
    EXPECT_FLOAT_EQ(itemFor(items, "C")->fromStart, 80.0f) << "C waits below the two originals";

    juce::Component host;
    host.setSize(300, 300);
    ExitEnterListMotion motion(host);
    ASSERT_TRUE(motion.start(jobFor(items)));
    motion.applyAtMs(0.0);
    EXPECT_FLOAT_EQ(motion.drawnRectFor("Ac")->getCentreY(), kHostY + 20.0f) << "Ac on A";
    EXPECT_FLOAT_EQ(motion.drawnRectFor("Bc")->getCentreY(), kHostY + 60.0f) << "Bc on B";
    motion.applyAtMs(ExitEnterTimeline::kOutOfSourceMs);
    EXPECT_FLOAT_EQ(motion.drawnRectFor("Ac")->getY(), kHostY + 80.0f);
    EXPECT_FLOAT_EQ(motion.drawnRectFor("Bc")->getY(), kHostY + 120.0f);
    EXPECT_FLOAT_EQ(motion.drawnRectFor("C")->getY(), kHostY + 160.0f);
}

TEST(ExitEnterListFromSource, AHorizontalListTravelsAlongX) {
    ReducedGuard guard(false);
    juce::Component host;
    host.setSize(400, 300);
    ExitEnterListMotion motion(host);
    ASSERT_TRUE(motion.start(jobFor(forwardItems(), ListAxis::Horizontal)));
    motion.applyAtMs(0.0);
    EXPECT_FLOAT_EQ(motion.drawnRectFor("Ac")->getCentreX(), 10.0f + 20.0f);
    motion.applyAtMs(ExitEnterTimeline::kOutOfSourceMs);
    EXPECT_FLOAT_EQ(motion.drawnRectFor("Ac")->getX(), 10.0f + 40.0f);
    EXPECT_FLOAT_EQ(motion.drawnRectFor("B")->getX(), 10.0f + 80.0f);
}
