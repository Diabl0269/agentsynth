// InsertGapPlanTests.cpp
//
// The pure insert-between geometry (InsertGapPlan.h,
// docs/layout/layout.md#making-room-for-a-module-dropped-between-others): which gap a pointer is over, where the new
// card lands, and exactly how far each unit moves. No canvas: layout units in, plan out.

#include "UI/Layout/InsertGap/InsertGapPlan.h"
#include <gtest/gtest.h>
#include <map>

namespace {
namespace ig = synth::insert_gap;
using synth::LayoutUtil::LayoutUnit;
using Rect = juce::Rectangle<int>;

constexpr int kW = 280, kH = 360, kSpacing = 40, kX0 = 40, kY0 = 40;
const juce::Point<int> kCard{kW, kH};

juce::String key(int i) { return "n:" + juce::String(i + 1); }

/** `n` cards in one row, kSpacing apart, starting at (x0, y). */
std::vector<LayoutUnit> row(int n, int x0 = kX0, int y = kY0, int spacing = kSpacing, int firstKey = 0) {
    std::vector<LayoutUnit> units;
    for (int i = 0; i < n; ++i)
        units.push_back({key(firstKey + i), Rect(x0 + i * (kW + spacing), y, kW, kH), false});
    return units;
}

std::map<juce::String, juce::Point<int>> deltas(const ig::Plan& plan) {
    std::map<juce::String, juce::Point<int>> out;
    for (const auto& m : plan.moves)
        out[m.key] = m.delta;
    return out;
}

juce::Point<int> deltaOf(const ig::Plan& plan, const juce::String& k) {
    const auto d = deltas(plan);
    const auto it = d.find(k);
    return it != d.end() ? it->second : juce::Point<int>();
}

/** The units after the plan's moves. */
std::vector<LayoutUnit> applied(std::vector<LayoutUnit> units, const ig::Plan& plan) {
    for (auto& u : units)
        u.rect += deltaOf(plan, u.key);
    return units;
}

bool overlapsAnyWithClearance(const std::vector<LayoutUnit>& units, Rect r) {
    for (const auto& u : units)
        if (r.expanded(synth::LayoutUtil::kCollisionGap - 1).intersects(u.rect))
            return true;
    return false;
}

} // namespace

// ---- Rows of 2, 3, 5 and 10: start, middle and the last gap -------------------------------------------------------

class InsertGapRow : public ::testing::TestWithParam<int> {};

TEST_P(InsertGapRow, InsertingInFrontOfEachCardMovesExactlyTheCardsAfterIt) {
    const int n = GetParam();
    const auto units = row(n);
    const int shift = ig::snapUp(kW + kSpacing);
    for (int k : {0, n / 2, n - 1}) {
        SCOPED_TRACE("insert in front of card " + std::to_string(k) + " of " + std::to_string(n));
        const auto plan = ig::planBefore(units, {ig::Axis::Row, key(k)}, kCard);
        ASSERT_TRUE(plan.has_value());
        EXPECT_EQ(plan->slot, Rect(units[(size_t)k].rect.getX(), kY0, kW, kH)) << "the card takes the anchor's place";
        EXPECT_EQ(plan->spacing, kSpacing) << "the row's own spacing is copied";
        EXPECT_EQ((int)plan->moves.size(), n - k) << "only the anchor and the cards after it move";
        for (int i = 0; i < n; ++i)
            EXPECT_EQ(deltaOf(*plan, key(i)), i < k ? juce::Point<int>() : juce::Point<int>(shift, 0)) << "card " << i;

        const auto after = applied(units, *plan);
        for (int i = k + 1; i < n; ++i)
            EXPECT_EQ(after[(size_t)i].rect.getX() - after[(size_t)i - 1].rect.getRight(), kSpacing)
                << "the spacing between the moved cards is kept";
        EXPECT_EQ(after[(size_t)k].rect.getX() - plan->slot.getRight(), kSpacing) << "and around the new card";
        if (k > 0)
            EXPECT_EQ(plan->slot.getX() - after[(size_t)k - 1].rect.getRight(), kSpacing);
        EXPECT_FALSE(overlapsAnyWithClearance(after, plan->slot)) << "the slot is free";
    }
}

TEST_P(InsertGapRow, PastTheLastCardNothingMoves) {
    const int n = GetParam();
    const auto units = row(n);
    const auto& last = units.back().rect;
    EXPECT_FALSE(ig::planAfter(units, key(n - 1), kCard).has_value());
    // The pointer in the last card's trailing edge band, and just past it.
    EXPECT_FALSE(ig::pickTarget(units, {last.getRight() - 10, last.getCentreY()}, kCard).has_value());
    EXPECT_FALSE(ig::pickTarget(units, {last.getRight() + 30, last.getCentreY()}, kCard).has_value());
}

TEST_P(InsertGapRow, ThePointerInEveryGapPicksTheCardAfterIt) {
    const int n = GetParam();
    const auto units = row(n);
    for (int k = 1; k < n; ++k) {
        const auto& a = units[(size_t)k - 1].rect;
        const auto& b = units[(size_t)k].rect;
        const int y = a.getCentreY();
        for (int x : {a.getRight() - 30, (a.getRight() + b.getX()) / 2, b.getX() + 30}) {
            const auto target = ig::pickTarget(units, {x, y}, kCard);
            ASSERT_TRUE(target.has_value()) << "x " << x << " gap " << k;
            EXPECT_EQ(target->axis, ig::Axis::Row);
            EXPECT_EQ(target->anchorKey, key(k));
        }
    }
    // In front of the first card.
    const auto first = ig::pickTarget(units, {units.front().rect.getX() - 30, kY0 + 100}, kCard);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->anchorKey, key(0));
}

INSTANTIATE_TEST_SUITE_P(Sizes, InsertGapRow, ::testing::Values(2, 3, 5, 10));

// ---- Spacing --------------------------------------------------------------------------------------------------------

TEST(InsertGapPlan, UnevenWidthsMoveByTheNewCardPlusTheSpacing) {
    std::vector<LayoutUnit> units = {
        {"a", Rect(40, 40, 560, 200)}, {"b", Rect(624, 40, 40, 40)}, {"c", Rect(720, 40, 280, 360)}};
    const auto plan = ig::planBefore(units, {ig::Axis::Row, "b"}, {200, 100});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->spacing, 24);
    EXPECT_EQ(plan->slot.getPosition(), juce::Point<int>(624, 40));
    EXPECT_EQ(deltaOf(*plan, "b"), juce::Point<int>(ig::snapUp(200 + 24), 0));
    EXPECT_EQ(deltaOf(*plan, "c"), juce::Point<int>(ig::snapUp(200 + 24), 0));
    EXPECT_EQ(deltaOf(*plan, "a"), juce::Point<int>());
}

TEST(InsertGapPlan, AWideGapAlreadyFitsSoNothingMoves) {
    std::vector<LayoutUnit> units = {{"a", Rect(40, 40, kW, kH)}, {"b", Rect(1000, 40, kW, kH)}};
    const auto plan = ig::planBefore(units, {ig::Axis::Row, "b"}, kCard);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->spacing, ig::kMaxSpacing) << "a huge gap is not copied";
    EXPECT_EQ(plan->slot.getX(), ig::snapUp(320 + ig::kMaxSpacing));
    EXPECT_TRUE(plan->moves.empty());
}

TEST(InsertGapPlan, CardsCloserThanTheClearanceGetTheClearanceAroundTheNewCard) {
    std::vector<LayoutUnit> units = {{"a", Rect(40, 40, kW, kH)}, {"b", Rect(324, 40, kW, kH)}};
    const auto plan = ig::planBefore(units, {ig::Axis::Row, "b"}, kCard);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->spacing, synth::LayoutUtil::kCollisionGap);
    EXPECT_EQ(plan->slot.getX(), ig::snapUp(320 + synth::LayoutUtil::kCollisionGap));
    EXPECT_FALSE(overlapsAnyWithClearance(applied(units, *plan), plan->slot));
}

TEST(InsertGapPlan, ATightRowMovesAsOneAndKeepsItsOwnSpacing) {
    const auto units = row(4, kX0, kY0, /*spacing=*/8);
    const auto plan = ig::planBefore(units, {ig::Axis::Row, key(1)}, kCard);
    ASSERT_TRUE(plan.has_value());
    const auto first = deltaOf(*plan, key(1));
    for (int i = 2; i < 4; ++i)
        EXPECT_EQ(deltaOf(*plan, key(i)), first) << "no card is shoved further for sitting close to the next";
}

TEST(InsertGapPlan, AtTheStartTheSpacingAfterTheFirstCardIsCopied) {
    const auto units = row(3, kX0, kY0, /*spacing=*/64);
    const auto plan = ig::planBefore(units, {ig::Axis::Row, key(0)}, kCard);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->spacing, 64);
    EXPECT_EQ(plan->slot.getX(), kX0);
    EXPECT_EQ(deltaOf(*plan, key(0)).x, ig::snapUp(kW + 64));
}

TEST(InsertGapPlan, ALoneCardUsesTheDefaultSpacing) {
    const std::vector<LayoutUnit> units = {{"a", Rect(40, 40, kW, kH)}};
    const auto plan = ig::planBefore(units, {ig::Axis::Row, "a"}, kCard);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->spacing, ig::kDefaultSpacing);
    EXPECT_EQ(deltaOf(*plan, "a").x, ig::snapUp(kW + ig::kDefaultSpacing));
}

// ---- Columns and mixed layouts ----------------------------------------------------------------------------------

TEST(InsertGapPlan, AColumnMovesTheCardsBelowDown) {
    std::vector<LayoutUnit> units;
    for (int i = 0; i < 4; ++i)
        units.push_back({key(i), Rect(40, 40 + i * (200 + 32), kW, 200)});
    units.push_back({"beside", Rect(400, 40 + 232, kW, 200)}); // a card to the right of the column: never moves
    const auto plan = ig::planBefore(units, {ig::Axis::Column, key(2)}, {kW, 120});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->spacing, 32);
    EXPECT_EQ(plan->slot, Rect(40, units[2].rect.getY(), kW, 120));
    const int shift = ig::snapUp(120 + 32);
    EXPECT_EQ(deltaOf(*plan, key(0)), juce::Point<int>());
    EXPECT_EQ(deltaOf(*plan, key(1)), juce::Point<int>());
    EXPECT_EQ(deltaOf(*plan, key(2)), juce::Point<int>(0, shift));
    EXPECT_EQ(deltaOf(*plan, key(3)), juce::Point<int>(0, shift));
    EXPECT_EQ(deltaOf(*plan, "beside"), juce::Point<int>());
}

TEST(InsertGapPlan, ThePointerBetweenStackedCardsPicksAColumn) {
    const std::vector<LayoutUnit> units = {{"top", Rect(40, 40, kW, 200)}, {"bottom", Rect(40, 272, kW, 200)}};
    const auto target = ig::pickTarget(units, {180, 256}, {kW, 120});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->axis, ig::Axis::Column);
    EXPECT_EQ(target->anchorKey, "bottom");
    // Inside the top card, near its bottom edge.
    const auto nearEdge = ig::pickTarget(units, {180, 225}, {kW, 120});
    ASSERT_TRUE(nearEdge.has_value());
    EXPECT_EQ(nearEdge->axis, ig::Axis::Column);
    EXPECT_EQ(nearEdge->anchorKey, "bottom");
}

TEST(InsertGapPlan, ThePointerDeepInsideACardIsAnOrdinaryDrop) {
    const auto units = row(3);
    EXPECT_FALSE(ig::pickTarget(units, units[1].rect.getCentre(), kCard).has_value());
}

TEST(InsertGapPlan, AFarAwayPointerIsNoGap) {
    const auto units = row(3);
    EXPECT_FALSE(ig::pickTarget(units, {3000, 3000}, kCard).has_value());
}

TEST(InsertGapPlan, ACardPartlyInTheRowMovesWithItAndOneBelowItDoesNot) {
    auto units = row(3);
    units.push_back({"partly", Rect(units[2].rect.getRight() + kSpacing, kY0 + 300, kW, kH)}); // overlaps rows 300..360
    units.push_back({"below", Rect(units[1].rect.getX(), kY0 + kH + 200, kW, kH)});            // the next row down
    const auto plan = ig::planBefore(units, {ig::Axis::Row, key(1)}, kCard);
    ASSERT_TRUE(plan.has_value());
    const int shift = ig::snapUp(kW + kSpacing);
    EXPECT_EQ(deltaOf(*plan, "partly"), juce::Point<int>(shift, 0));
    EXPECT_EQ(deltaOf(*plan, "below"), juce::Point<int>()) << "not in the row and not run into";
}

// ---- Chain push ----------------------------------------------------------------------------------------------------

TEST(InsertGapPlan, ACrowdedCanvasPushesInChainPastTheRowEnd) {
    auto units = row(3); // x 40..960
    const int end = units[2].rect.getRight();
    units.push_back({"tall", Rect(end + kSpacing, kY0, kW, kH + 400)});              // in the row, reaching down
    units.push_back({"e", Rect(end + kSpacing + kW + kSpacing, kY0 + 600, kW, kH)}); // right of tall, below the row
    units.push_back({"f", Rect(end + kSpacing + 2 * (kW + kSpacing), kY0 + 600, kW, kH)}); // e's own row-mate
    units.push_back({"g", Rect(kX0, kY0 + 600, kW, kH)});                                  // below the row, far left
    const auto plan = ig::planBefore(units, {ig::Axis::Row, key(1)}, kCard);
    ASSERT_TRUE(plan.has_value());
    const auto after = applied(units, *plan);
    for (size_t i = 0; i < after.size(); ++i)
        for (size_t j = i + 1; j < after.size(); ++j)
            EXPECT_FALSE(after[i].rect.expanded(synth::LayoutUtil::kCollisionGap - 1).intersects(after[j].rect))
                << after[i].key << " and " << after[j].key << " were pushed onto each other";
    EXPECT_FALSE(overlapsAnyWithClearance(after, plan->slot));
    const int shift = ig::snapUp(kW + kSpacing);
    EXPECT_EQ(deltaOf(*plan, "tall").x, shift);
    // e sat 40 right of tall; tall now ends 280 further right than e's left edge, plus the clearance.
    EXPECT_EQ(deltaOf(*plan, "e").x, ig::snapUp(shift + synth::LayoutUtil::kCollisionGap - kSpacing))
        << "the tall card pushed the one it ran into, which is not in the row";
    EXPECT_GT(deltaOf(*plan, "f").x, 0) << "and that one pushed its neighbour";
    EXPECT_EQ(deltaOf(*plan, "g"), juce::Point<int>()) << "nothing ran into it";
    for (const auto& m : plan->moves)
        EXPECT_EQ(m.delta.y, 0) << m.key << ": a row insert only ever pushes right";
}

TEST(InsertGapPlan, ATallNewCardPushesTheCardBelowTheSlotAlong) {
    auto units = row(3);
    units.push_back({"under", Rect(units[1].rect.getX() + 40, kY0 + kH + 40, kW, 200)}); // under card 1
    units.push_back({"underLeft", Rect(kX0, kY0 + kH + 40, 200, 200)}); // under card 0: behind the slot
    const auto plan = ig::planBefore(units, {ig::Axis::Row, key(1)}, {kW, kH + 200});
    ASSERT_TRUE(plan.has_value());
    EXPECT_GT(deltaOf(*plan, "under").x, 0);
    EXPECT_EQ(deltaOf(*plan, "underLeft"), juce::Point<int>()) << "never pushed back past the slot";
    EXPECT_FALSE(overlapsAnyWithClearance(applied(units, *plan), plan->slot));
}

TEST(InsertGapPlan, CardsNearTheCanvasEdgeStayOnTheCanvas) {
    const auto units = row(3, /*x0=*/0, /*y=*/0);
    const auto plan = ig::planBefore(units, {ig::Axis::Row, key(0)}, kCard);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->slot.getPosition(), juce::Point<int>(0, 0));
    for (const auto& u : applied(units, *plan))
        EXPECT_GE(u.rect.getX(), 0);
}

TEST(InsertGapPlan, APinnedUnitNeverMovesAndIsNeverAnAnchor) {
    auto units = row(3);
    units.back().pinned = true;
    const auto plan = ig::planBefore(units, {ig::Axis::Row, key(1)}, kCard);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(deltaOf(*plan, key(2)), juce::Point<int>());
    EXPECT_FALSE(ig::planBefore(units, {ig::Axis::Row, key(2)}, kCard).has_value());
    EXPECT_FALSE(ig::pickTarget(units, {units[2].rect.getX() + 20, kY0 + 100}, kCard).has_value());
}

TEST(InsertGapPlan, PlanAfterPutsTheCardRightAfterTheGivenOne) {
    const auto units = row(4);
    const auto plan = ig::planAfter(units, key(1), kCard);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->target.anchorKey, key(2));
    EXPECT_EQ(plan->slot.getX(), units[2].rect.getX());
}

TEST(InsertGapPlan, AGrownUnitPushesWhatIsAheadOfIt) {
    std::vector<LayoutUnit> units = {{"behind", Rect(0, 0, 100, 100)},
                                     {"ahead", Rect(600, 40, 200, 200)},
                                     {"aheadOfThat", Rect(840, 40, 200, 200)},
                                     {"farBelow", Rect(600, 2000, 200, 200)}};
    const Rect before(120, 40, 400, 300), after(120, 40, 720, 300);
    const auto moves = ig::pushAhead(units, before, after, ig::Axis::Row);
    std::map<juce::String, juce::Point<int>> d;
    for (const auto& m : moves)
        d[m.key] = m.delta;
    EXPECT_EQ(d.count("behind"), 0u);
    EXPECT_EQ(d.count("farBelow"), 0u);
    ASSERT_EQ(d.count("ahead"), 1u);
    EXPECT_EQ(d["ahead"], juce::Point<int>(ig::snapUp(840 + 12 - 600), 0));
    EXPECT_GT(d["aheadOfThat"].x, 0) << "in chain";
    EXPECT_TRUE(ig::pushAhead(units, before, before, ig::Axis::Row).empty()) << "nothing grew, nothing moves";
}

TEST(InsertGapPlan, SnapUpRoundsToTheGrid) {
    EXPECT_EQ(ig::snapUp(0), 0);
    EXPECT_EQ(ig::snapUp(-5), 0);
    EXPECT_EQ(ig::snapUp(1), 8);
    EXPECT_EQ(ig::snapUp(8), 8);
    EXPECT_EQ(ig::snapUp(321), 328);
}
