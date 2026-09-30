// resolveDisplacement: the pure make-room-when-something-grows core (LayoutUtil.h).

#include "UI/Layout/LayoutUtil.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <random>

using namespace synth::LayoutUtil;

namespace {
LayoutUnit unit(const char* key, int x, int y, int w = 100, int h = 100, bool pinned = false) {
    return {key, {x, y, w, h}, pinned};
}

juce::Point<int> deltaOf(const std::vector<UnitMove>& moves, const char* key) {
    for (const auto& m : moves)
        if (m.key == key)
            return m.delta;
    return {};
}

std::vector<LayoutUnit> applyMoves(std::vector<LayoutUnit> units, const std::vector<UnitMove>& moves) {
    for (auto& u : units)
        u.rect.translate(deltaOf(moves, u.key.toRawUTF8()).x, deltaOf(moves, u.key.toRawUTF8()).y);
    return units;
}

// After applying, nothing movable overlaps the grower or another moved unit.
void expectClear(const std::vector<LayoutUnit>& before, const std::vector<UnitMove>& moves, const char* grower) {
    const auto after = applyMoves(before, moves);
    for (size_t i = 0; i < after.size(); ++i)
        for (size_t j = i + 1; j < after.size(); ++j) {
            if (after[i].pinned && after[j].pinned)
                continue;
            EXPECT_FALSE(after[i].rect.intersects(after[j].rect))
                << after[i].key << " overlaps " << after[j].key << " (grower " << grower << ")";
        }
}
} // namespace

TEST(LayoutUtilDisplacement, NoOverlapReturnsEmpty) {
    const std::vector<LayoutUnit> units{unit("g", 100, 100), unit("a", 400, 100)};
    EXPECT_TRUE(resolveDisplacement("g", units).empty());
}

TEST(LayoutUtilDisplacement, PushedRightWhenThatIsTheLeastPenetration) {
    // a overlaps g's right edge by 20: right needs 20+gap, down needs ~112.
    const std::vector<LayoutUnit> units{unit("g", 100, 100), unit("a", 180, 100)};
    const auto moves = resolveDisplacement("g", units);
    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves[0].key, "a");
    EXPECT_EQ(moves[0].delta.y, 0);
    EXPECT_GT(moves[0].delta.x, 0);
    EXPECT_EQ(moves[0].delta.x % kGridSize, 0);
    expectClear(units, moves, "g");
}

TEST(LayoutUtilDisplacement, PushedDownWhenThatIsTheLeastPenetration) {
    const std::vector<LayoutUnit> units{unit("g", 100, 100), unit("a", 100, 180)};
    const auto moves = resolveDisplacement("g", units);
    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves[0].delta.x, 0);
    EXPECT_GT(moves[0].delta.y, 0);
    EXPECT_EQ(moves[0].delta.y % kGridSize, 0);
    expectClear(units, moves, "g");
}

TEST(LayoutUtilDisplacement, TieGoesDownThenRight) {
    // a sits diagonally so right and down penetrate equally.
    const std::vector<LayoutUnit> units{unit("g", 100, 100), unit("a", 180, 180)};
    const auto moves = resolveDisplacement("g", units);
    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves[0].delta.x, 0);
    EXPECT_GT(moves[0].delta.y, 0);
}

TEST(LayoutUtilDisplacement, CascadeInheritsDirectionAlongARow) {
    const std::vector<LayoutUnit> units{unit("g", 100, 100), unit("a", 190, 100), unit("b", 310, 100),
                                        unit("c", 430, 100)};
    const auto moves = resolveDisplacement("g", units);
    ASSERT_EQ(moves.size(), 3u);
    for (const auto& m : moves) {
        EXPECT_GT(m.delta.x, 0);
        EXPECT_EQ(m.delta.y, 0);
    }
    expectClear(units, moves, "g");
}

TEST(LayoutUtilDisplacement, TwoChainsConvergeOnMaxPerAxis) {
    // a is pushed right by g; b sits below-right so g pushes it too; c is shoved by both.
    const std::vector<LayoutUnit> units{unit("g", 100, 100, 100, 200), unit("a", 190, 100), unit("b", 190, 210),
                                        unit("c", 320, 150, 100, 100)};
    const auto moves = resolveDisplacement("g", units);
    expectClear(units, moves, "g");
    const auto a = deltaOf(moves, "a"), b = deltaOf(moves, "b"), c = deltaOf(moves, "c");
    EXPECT_GT(c.x, 0);
    EXPECT_GE(c.x, std::max(a.x, b.x) - kGridSize * 20); // moved at least as far as needed to clear the further one
    const auto after = applyMoves(units, moves);
    EXPECT_GE(after[3].rect.getX(), std::max(after[1].rect.getRight(), after[2].rect.getRight()) + kCollisionGap);
}

TEST(LayoutUtilDisplacement, PinnedNeverMovesAndPushesRedirect) {
    // Right of a is a pinned unit, so a (least penetration: right) must go down instead.
    const std::vector<LayoutUnit> units{unit("g", 100, 100), unit("a", 180, 100), unit("p", 300, 100, 100, 100, true)};
    const auto moves = resolveDisplacement("g", units);
    EXPECT_EQ(deltaOf(moves, "p"), juce::Point<int>());
    const auto a = deltaOf(moves, "a");
    EXPECT_EQ(a.x, 0);
    EXPECT_GT(a.y, 0);
    expectClear(units, moves, "g");
}

TEST(LayoutUtilDisplacement, LeftWallSendsNeighbourDownNotAcrossTheGrower) {
    // The grower has expanded leftward over a neighbour at x=40. Moving left would leave the canvas, so it
    // keeps its x and goes down.
    const std::vector<LayoutUnit> units{unit("g", 0, 100, 400, 120), unit("a", 40, 130, 100, 60)};
    const auto moves = resolveDisplacement("g", units);
    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves[0].delta.x, 0);
    EXPECT_GT(moves[0].delta.y, 0);
    expectClear(units, moves, "g");
}

TEST(LayoutUtilDisplacement, NeverJumpsToNegativeCoordinates) {
    const std::vector<LayoutUnit> units{unit("g", 100, 100), unit("a", 20, 90, 100, 100)};
    const auto after = applyMoves(units, resolveDisplacement("g", units));
    EXPECT_GE(after[1].rect.getX(), 0);
    EXPECT_GE(after[1].rect.getY(), 0);
}

TEST(LayoutUtilDisplacement, DeterministicUnderShuffledInput) {
    std::vector<LayoutUnit> units{unit("g", 100, 100, 200, 200), unit("a", 250, 120), unit("b", 250, 240),
                                  unit("c", 380, 130),           unit("d", 120, 280), unit("e", 300, 300)};
    auto sortedMoves = [](std::vector<UnitMove> m) {
        std::sort(m.begin(), m.end(), [](const UnitMove& x, const UnitMove& y) { return x.key < y.key; });
        return m;
    };
    const auto reference = sortedMoves(resolveDisplacement("g", units));
    std::mt19937 rng(42);
    for (int i = 0; i < 20; ++i) {
        std::shuffle(units.begin(), units.end(), rng);
        const auto moves = sortedMoves(resolveDisplacement("g", units));
        ASSERT_EQ(moves.size(), reference.size());
        for (size_t k = 0; k < moves.size(); ++k) {
            EXPECT_EQ(moves[k].key, reference[k].key);
            EXPECT_EQ(moves[k].delta, reference[k].delta);
        }
    }
    expectClear(units, reference, "g");
}

TEST(LayoutUtilDisplacement, GrowerNeverMovesAndUnknownGrowerIsANoOp) {
    const std::vector<LayoutUnit> units{unit("g", 100, 100), unit("a", 150, 100)};
    EXPECT_EQ(deltaOf(resolveDisplacement("g", units), "g"), juce::Point<int>());
    EXPECT_TRUE(resolveDisplacement("nope", units).empty());
}
