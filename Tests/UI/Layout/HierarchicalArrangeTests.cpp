// HierarchicalArrangeTests.cpp
//
// The pure layout behind auto-arrange (UI/Layout/HierarchicalArrange.h): rows of tracks, columns of stages aligned
// across rows, shared modulators on top, macros as blocks, idempotence. The canvas half is covered through a real
// GraphEditor in Tests/UI/Graph/AutoArrangeTests.cpp. (docs/layout/layout.md#auto-arrange)

#include "UI/Layout/HierarchicalArrange.h"
#include <algorithm>
#include <gtest/gtest.h>

namespace {
using namespace synth::LayoutUtil;

ArrangeBlock module(const juce::String& id, int w = 280, int h = 300, long long order = 0, int rank = 4) {
    ArrangeBlock b;
    b.id = id;
    b.size = {w, h};
    b.order = order;
    b.roleRank = rank;
    return b;
}

ArrangeBlock card(const juce::String& id, int w = 200, int h = 90, long long order = 0) {
    auto b = module(id, w, h, order);
    b.kind = ArrangeKind::CollapsedMacro;
    return b;
}

ArrangeBlock openMacro(const juce::String& id, std::vector<ArrangeBlock> children, long long order = 0) {
    ArrangeBlock b;
    b.id = id;
    b.kind = ArrangeKind::OpenMacro;
    b.order = order;
    b.children = std::move(children);
    return b;
}

ArrangeEdge flow(const juce::String& a, const juce::String& b) { return {a, b, false}; }
ArrangeEdge mod(const juce::String& a, const juce::String& b) { return {a, b, true}; }

// Three identical tracks: in -> gate -> eq -> comp, ids "<stage><track>".
ArrangeInput threeTracks() {
    ArrangeInput in;
    long long order = 1;
    for (int t = 1; t <= 3; ++t) {
        const auto s = juce::String(t);
        in.blocks.push_back(module("in" + s, 280, 200, order++));
        in.blocks.push_back(module("gate" + s, 280, 300, order++, 2));
        in.blocks.push_back(module("eq" + s, 560, 300, order++, 1));
        in.blocks.push_back(module("comp" + s, 280, 300, order++, 2));
        in.edges.push_back(flow("in" + s, "gate" + s));
        in.edges.push_back(flow("gate" + s, "eq" + s));
        in.edges.push_back(flow("eq" + s, "comp" + s));
        in.trackStarts.push_back("in" + s);
    }
    return in;
}

juce::Rectangle<int> rectOf(const ArrangeInput& in, const ArrangeOutput& out, const juce::String& id) {
    for (const auto& b : in.blocks)
        if (b.id == id)
            return {out.positions.at(id), juce::Point<int>(out.positions.at(id)) + b.size};
    return {};
}

// Every leaf rectangle (modules, cards) and every open hull, at every level.
void collectRects(const ArrangeBlock& b, const ArrangeOutput& out,
                  std::vector<std::pair<juce::String, juce::Rectangle<int>>>& rects) {
    if (b.kind == ArrangeKind::OpenMacro && !b.children.empty()) {
        for (const auto& c : b.children)
            collectRects(c, out, rects);
        return;
    }
    rects.push_back({b.id, {out.positions.at(b.id), juce::Point<int>(out.positions.at(b.id)) + b.size}});
}

void expectTopLevelClear(const ArrangeInput& in, const ArrangeOutput& out) {
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> footprints;
    for (const auto& b : in.blocks) {
        if (auto h = out.hulls.find(b.id); h != out.hulls.end())
            footprints.push_back({b.id, h->second});
        else
            footprints.push_back({b.id, {out.positions.at(b.id), juce::Point<int>(out.positions.at(b.id)) + b.size}});
    }
    for (size_t i = 0; i < footprints.size(); ++i)
        for (size_t j = i + 1; j < footprints.size(); ++j)
            EXPECT_FALSE(footprints[i]
                             .second.expanded(kCollisionGap / 2)
                             .intersects(footprints[j].second.expanded(kCollisionGap / 2)))
                << footprints[i].first << " overlaps " << footprints[j].first;
}
} // namespace

TEST(HierarchicalArrange, GoldenThreeTracksWithASharedModulatorOnTop) {
    auto in = threeTracks();
    in.blocks.push_back(module("lfo", 280, 300, 100, 3));
    in.edges.push_back(mod("lfo", "gate1"));
    in.edges.push_back(mod("lfo", "gate2"));

    const auto out = computeHierarchicalArrange(in);

    // The modulator feeds two rows, so it is on top; then the tracks in track order.
    EXPECT_EQ(out.positions.at("lfo").y, kArrangeOriginY);
    EXPECT_EQ(out.firstRowY, kArrangeOriginY);
    EXPECT_LT(out.positions.at("lfo").y, out.positions.at("in1").y);
    EXPECT_LT(out.positions.at("in1").y, out.positions.at("in2").y);
    EXPECT_LT(out.positions.at("in2").y, out.positions.at("in3").y);

    // A stage is one x for every track, and stages run left to right.
    for (const char* stage : {"in", "gate", "eq", "comp"})
        for (int t = 2; t <= 3; ++t)
            EXPECT_EQ(out.positions.at(juce::String(stage) + juce::String(t)).x,
                      out.positions.at(juce::String(stage) + "1").x)
                << stage;
    EXPECT_LT(out.positions.at("in1").x, out.positions.at("gate1").x);
    EXPECT_LT(out.positions.at("gate1").x, out.positions.at("eq1").x);
    EXPECT_LT(out.positions.at("eq1").x, out.positions.at("comp1").x);

    // The modulator is not part of any track's columns: it sits over the first column, cross-row edges never shift it.
    EXPECT_EQ(out.positions.at("lfo").x, out.positions.at("in1").x);

    for (const auto& [id, pos] : out.positions) {
        EXPECT_EQ(pos.x % kGridSize, 0) << id;
        EXPECT_EQ(pos.y % kGridSize, 0) << id;
    }
    expectTopLevelClear(in, out);
}

TEST(HierarchicalArrange, TrackOrderDecidesRowOrderNotTheOrderOfTheBlocks) {
    auto in = threeTracks();
    in.trackStarts = {"in3", "in1", "in2"};

    const auto out = computeHierarchicalArrange(in);

    EXPECT_LT(out.positions.at("in3").y, out.positions.at("in1").y);
    EXPECT_LT(out.positions.at("in1").y, out.positions.at("in2").y);
}

TEST(HierarchicalArrange, ArrangingTwiceGivesTheSameResult) {
    auto in = threeTracks();
    in.blocks.push_back(module("lfo", 280, 300, 100, 3));
    in.edges.push_back(mod("lfo", "gate1"));
    in.edges.push_back(mod("lfo", "gate2"));
    in.blocks.push_back(module("loose", 280, 300, 200));

    const auto first = computeHierarchicalArrange(in);
    const auto second = computeHierarchicalArrange(in);

    EXPECT_EQ(first.positions, second.positions);
    EXPECT_EQ(first.hulls, second.hulls);
}

TEST(HierarchicalArrange, TheInputOrderOfBlocksAndEdgesDoesNotMatter) {
    auto in = threeTracks();
    auto shuffled = in;
    std::reverse(shuffled.blocks.begin(), shuffled.blocks.end());
    std::reverse(shuffled.edges.begin(), shuffled.edges.end());

    EXPECT_EQ(computeHierarchicalArrange(in).positions, computeHierarchicalArrange(shuffled).positions);
}

TEST(HierarchicalArrange, CrossRowEdgesNeverInfluencePlacement) {
    auto in = threeTracks();
    const auto without = computeHierarchicalArrange(in);
    // Track 1's eq modulates track 3's gate and track 2's comp: drawn, but the placement stays as it was.
    in.edges.push_back(mod("eq1", "gate3"));
    in.edges.push_back(mod("eq1", "comp2"));

    EXPECT_EQ(computeHierarchicalArrange(in).positions, without.positions);
}

TEST(HierarchicalArrange, AModulatorForOneRowJoinsThatRowInsteadOfTheSharedRow) {
    auto in = threeTracks();
    in.blocks.push_back(module("lfo", 280, 300, 100, 3));
    in.edges.push_back(mod("lfo", "gate2"));

    const auto out = computeHierarchicalArrange(in);

    // It stacks in track 2's row, above that row's own first card, and track 1 stays above it all.
    EXPECT_GT(out.positions.at("lfo").y, out.positions.at("in1").y);
    EXPECT_LT(out.positions.at("lfo").y, out.positions.at("in2").y);
    EXPECT_LT(out.positions.at("in2").y, out.positions.at("in3").y);
    EXPECT_EQ(out.positions.at("lfo").x, out.positions.at("in2").x);
}

TEST(HierarchicalArrange, SharedModulatorsAreOrderedByConsumerCountThenOrder) {
    auto in = threeTracks();
    in.blocks.push_back(module("few", 280, 100, 1, 3));
    in.blocks.push_back(module("many", 280, 100, 2, 3));
    in.edges.push_back(mod("few", "gate1"));
    in.edges.push_back(mod("few", "gate2"));
    in.edges.push_back(mod("many", "gate1"));
    in.edges.push_back(mod("many", "gate2"));
    in.edges.push_back(mod("many", "gate3"));

    const auto out = computeHierarchicalArrange(in);

    EXPECT_LT(out.positions.at("many").y, out.positions.at("few").y);
    EXPECT_LT(out.positions.at("few").y, out.positions.at("in1").y);
}

TEST(HierarchicalArrange, RemainingComponentsGetOneRowEachAfterTheTracksOrderedByTheirLeftmostSource) {
    ArrangeInput in;
    in.blocks = {module("t", 280, 200, 1), module("b", 280, 200, 9),  module("b2", 280, 200, 10),
                 module("a", 280, 200, 5), module("a2", 280, 200, 6), module("solo", 280, 200, 3)};
    in.edges = {flow("b", "b2"), flow("a", "a2")};
    in.trackStarts = {"t"};

    const auto out = computeHierarchicalArrange(in);

    EXPECT_EQ(out.positions.at("t").y, kArrangeOriginY);
    EXPECT_LT(out.positions.at("t").y, out.positions.at("solo").y);
    EXPECT_LT(out.positions.at("solo").y, out.positions.at("a").y);
    EXPECT_LT(out.positions.at("a").y, out.positions.at("b").y);
    EXPECT_EQ(out.positions.at("a").y, out.positions.at("a2").y);
    EXPECT_GT(out.positions.at("a2").x, out.positions.at("a").x);
}

TEST(HierarchicalArrange, EndpointsThatResolveToNothingAreDropped) {
    auto in = threeTracks();
    const auto without = computeHierarchicalArrange(in);
    in.edges.push_back(flow("comp1", "master"));
    in.edges.push_back(flow("ghost", "in2"));

    EXPECT_EQ(computeHierarchicalArrange(in).positions, without.positions);
}

TEST(HierarchicalArrange, AnAliasStandsInForAHiddenMemberOrAPort) {
    ArrangeInput in;
    in.blocks = {module("src", 280, 200, 1), card("m:box", 200, 90, 2), module("dst", 280, 200, 3)};
    in.aliases = {{"n:hidden", "m:box"}};
    in.edges = {flow("src", "n:hidden"), flow("n:hidden", "dst")};
    in.trackStarts = {"src"};

    const auto out = computeHierarchicalArrange(in);

    EXPECT_LT(out.positions.at("src").x, out.positions.at("m:box").x);
    EXPECT_LT(out.positions.at("m:box").x, out.positions.at("dst").x);
    EXPECT_EQ(out.positions.at("src").y, out.positions.at("dst").y) << "one row";
}

TEST(HierarchicalArrange, ACycleStillLaysOut) {
    ArrangeInput in;
    in.blocks = {module("a", 280, 200, 1), module("b", 280, 200, 2), module("c", 280, 200, 3)};
    in.edges = {flow("a", "b"), flow("b", "c"), flow("c", "a")};

    const auto out = computeHierarchicalArrange(in);

    EXPECT_EQ(out.positions.size(), 3u);
    expectTopLevelClear(in, out);
}

TEST(HierarchicalArrange, ACollapsedMacroIsOneBlockTheSizeOfItsCard) {
    ArrangeInput in;
    in.blocks = {module("in", 280, 200, 1), card("m:strip", 240, 96, 2), module("next", 280, 200, 3)};
    in.edges = {flow("in", "m:strip"), flow("m:strip", "next")};
    in.trackStarts = {"in"};

    const auto out = computeHierarchicalArrange(in);

    // Column width follows the card, and its neighbours clear it by the column gap.
    const auto cardRect = rectOf(in, out, "m:strip");
    EXPECT_EQ(cardRect.getWidth(), 240);
    EXPECT_EQ(cardRect.getHeight(), 96);
    EXPECT_GE(out.positions.at("next").x - cardRect.getRight(), kLayerGapX - kGridSize);
    expectTopLevelClear(in, out);
}

TEST(HierarchicalArrange, AnOpenMacroIsLaidOutInsideAndPlacedAsOneHullBlock) {
    auto chain =
        openMacro("m:chan", {module("g", 280, 300, 1, 2), module("e", 560, 300, 2, 1), module("c", 280, 300, 3, 2)}, 1);
    chain.portRows = 2;
    ArrangeInput in;
    in.blocks = {module("in", 280, 200, 0), chain, module("other", 280, 200, 50)};
    in.edges = {flow("in", "g"), flow("g", "e"), flow("e", "c")};
    in.trackStarts = {"in"};

    const auto out = computeHierarchicalArrange(in);

    // The interior is a chain; the hull is exactly openMacroHull of the union of its members.
    const auto g = juce::Rectangle<int>(out.positions.at("g"),
                                        juce::Point<int>(out.positions.at("g")) + juce::Point<int>(280, 300));
    const auto e = juce::Rectangle<int>(out.positions.at("e"),
                                        juce::Point<int>(out.positions.at("e")) + juce::Point<int>(560, 300));
    const auto c = juce::Rectangle<int>(out.positions.at("c"),
                                        juce::Point<int>(out.positions.at("c")) + juce::Point<int>(280, 300));
    EXPECT_LT(g.getRight(), e.getX());
    EXPECT_LT(e.getRight(), c.getX());
    EXPECT_EQ(out.hulls.at("m:chan"), openMacroHull(g.getUnion(e).getUnion(c), 2));
    for (const auto& r : {g, e, c})
        EXPECT_TRUE(out.hulls.at("m:chan").contains(r));

    // The hull keeps kMacroHullSideOutset clear of the canvas edge, members sit on the grid, and nothing else lands in
    // it.
    EXPECT_GE(out.hulls.at("m:chan").getX(), 0);
    for (const auto& r : {g, e, c}) {
        EXPECT_EQ(r.getX() % kGridSize, 0);
        EXPECT_EQ(r.getY() % kGridSize, 0);
    }
    expectTopLevelClear(in, out);
}

TEST(HierarchicalArrange, ANestedOpenMacroSitsInsideItsParentsHull) {
    auto inner = openMacro("m:inner", {module("i1", 280, 300, 1), module("i2", 280, 300, 2)}, 1);
    inner.portRows = 1;
    auto outer = openMacro("m:outer", {inner, module("o1", 280, 300, 3), card("m:card", 200, 90, 4)}, 1);
    ArrangeInput in;
    in.blocks = {outer, module("beside", 280, 300, 9)};
    in.edges = {flow("i1", "i2"), flow("i2", "o1")};

    const auto out = computeHierarchicalArrange(in);

    const auto innerHull = out.hulls.at("m:inner");
    const auto outerHull = out.hulls.at("m:outer");
    EXPECT_TRUE(outerHull.contains(innerHull));
    // The outer hull is the union of its members with the inner HULL, not the inner members, grown once more.
    EXPECT_GT(innerHull.getX() - outerHull.getX(), kMacroHullSideOutset - 1);
    // No outer member lands inside the inner hull.
    for (const char* id : {"o1"}) {
        const juce::Rectangle<int> r(out.positions.at(id),
                                     juce::Point<int>(out.positions.at(id)) + juce::Point<int>(280, 300));
        EXPECT_FALSE(r.intersects(innerHull)) << id;
    }
    const juce::Rectangle<int> cardRect(out.positions.at("m:card"),
                                        juce::Point<int>(out.positions.at("m:card")) + juce::Point<int>(200, 90));
    EXPECT_FALSE(cardRect.intersects(innerHull));
    expectTopLevelClear(in, out);
}

TEST(HierarchicalArrange, AMacroOfPortsOnlyStaysARigidRectangle) {
    ArrangeInput in;
    in.blocks = {card("m:ports", 300, 120, 1), module("x", 280, 200, 2)};

    const auto out = computeHierarchicalArrange(in);

    EXPECT_TRUE(out.hulls.empty());
    expectTopLevelClear(in, out);
}

TEST(HierarchicalArrange, OpenMacroHullMatchesTheDrawnGeometry) {
    const juce::Rectangle<int> members(100, 200, 500, 300);

    const auto hull = openMacroHull(members, 0);

    EXPECT_EQ(hull.getX(), members.getX() - kMacroHullSideOutset);
    EXPECT_EQ(hull.getRight(), members.getRight() + kMacroHullSideOutset);
    EXPECT_EQ(hull.getY(), members.getY() - kHullChipRow);
    EXPECT_EQ(hull.getBottom(), members.getBottom() + kHullMargin);
    // Many port rows outgrow the members: the hull grows down to hold them and the footer.
    const auto tall = openMacroHull(juce::Rectangle<int>(100, 200, 500, 20), 10);
    EXPECT_EQ(tall.getBottom(),
              tall.getY() + kHullChipRow + kHullPortRowsBelowChip + 10 * kHullPortRowHeight + kHullPortFooter);
}

// A source block that only feeds one row is placed as late as possible: one column before its nearest consumer, not
// at column 0 over the row start.
TEST(HierarchicalArrange, AModulatorFeedingOnlyOneConsumerSitsInTheColumnBeforeIt) {
    ArrangeInput in;
    in.blocks = {module("in", 280, 200, 1), module("a", 280, 300, 2, 1), module("b", 280, 300, 3, 1),
                 module("c", 280, 300, 4, 2), module("lfo", 280, 300, 100, 3)};
    in.edges = {flow("in", "a"), flow("a", "b"), flow("b", "c"), mod("lfo", "c")};
    in.trackStarts = {"in"};

    const auto out = computeHierarchicalArrange(in);

    EXPECT_EQ(out.positions.at("lfo").x, out.positions.at("b").x) << "the column right before its consumer";
    EXPECT_LT(out.positions.at("lfo").x, out.positions.at("c").x);
    EXPECT_GT(out.positions.at("lfo").x, out.positions.at("in").x);
    EXPECT_EQ(out.positions.at("lfo").y, out.positions.at("c").y)
        << "level with its consumer, not at the column's bottom";
    expectTopLevelClear(in, out);

    const auto again = computeHierarchicalArrange(in);
    EXPECT_EQ(again.positions, out.positions);
}

// With several consumers the nearest one decides; the row start itself never moves.
TEST(HierarchicalArrange, TheNearestConsumerDecidesAndTheRowStartStaysPut) {
    ArrangeInput in;
    in.blocks = {module("in", 280, 200, 1), module("a", 280, 300, 2, 1), module("b", 280, 300, 3, 1),
                 module("c", 280, 300, 4, 2), module("lfo", 280, 300, 100, 3)};
    in.edges = {flow("in", "a"), flow("a", "b"), flow("b", "c"), mod("lfo", "b"), mod("lfo", "c")};
    in.trackStarts = {"in"};

    const auto out = computeHierarchicalArrange(in);

    EXPECT_EQ(out.positions.at("lfo").x, out.positions.at("a").x);
    EXPECT_EQ(out.positions.at("in").x, kArrangeOriginX);
}

// Macro -> modulator (MIDI retrigger) and modulator -> macro form a cycle at the macro level: the modulator stays one
// column before the macro and the blocks after the macro keep their columns.
TEST(HierarchicalArrange, AModulatorFedByItsOwnMacroStaysOneColumnBeforeIt) {
    ArrangeInput in;
    in.blocks = {module("in", 280, 200, 1), module("pre", 280, 300, 2, 1), card("macro", 280, 90, 3),
                 module("post", 280, 300, 4, 2), module("lfo", 280, 300, 100, 3)};
    in.edges = {flow("in", "pre"), flow("pre", "macro"), flow("macro", "post"), flow("macro", "lfo"),
                mod("lfo", "macro")};
    in.trackStarts = {"in"};

    const auto out = computeHierarchicalArrange(in);

    EXPECT_EQ(out.positions.at("lfo").x, out.positions.at("pre").x);
    EXPECT_LT(out.positions.at("pre").x, out.positions.at("macro").x);
    EXPECT_LT(out.positions.at("macro").x, out.positions.at("post").x);
    EXPECT_EQ(out.positions.at("lfo").y, out.positions.at("macro").y);
    EXPECT_EQ(computeHierarchicalArrange(in).positions, out.positions);
}

// One modulator feeding two macros of the same row sits before the nearer one.
TEST(HierarchicalArrange, AModulatorFeedingTwoMacrosOfOneRowSitsBeforeTheNearerOne) {
    ArrangeInput in;
    in.blocks = {module("in", 280, 200, 1), card("m1", 280, 90, 2), card("m2", 280, 90, 3),
                 module("lfo", 280, 300, 100, 3)};
    in.edges = {flow("in", "m1"), flow("m1", "m2"), mod("lfo", "m1"), mod("lfo", "m2")};
    in.trackStarts = {"in"};

    const auto out = computeHierarchicalArrange(in);

    EXPECT_EQ(out.positions.at("lfo").x, out.positions.at("in").x);
    EXPECT_LT(out.positions.at("lfo").x, out.positions.at("m1").x);
}

// A modulator's row follows what it modulates: MIDI from track 1's start must not pull an LFO that only modulates track
// 2's macro into track 1's row.
TEST(HierarchicalArrange, AModulatorFedFromAnotherRowLandsInItsConsumersRow) {
    ArrangeInput in;
    in.blocks = {module("in1", 280, 200, 1), card("m1", 280, 90, 2), module("in2", 280, 200, 3), card("m2", 280, 90, 4),
                 module("lfo", 280, 300, 100, 3)};
    in.edges = {flow("in1", "m1"), flow("in2", "m2"), flow("in1", "lfo"), mod("lfo", "m2")};
    in.trackStarts = {"in1", "in2"};

    const auto out = computeHierarchicalArrange(in);

    EXPECT_GT(out.positions.at("lfo").y, out.positions.at("m1").y + 90) << "below track 1's row";
    EXPECT_EQ(out.positions.at("lfo").y, out.positions.at("m2").y);
    EXPECT_LT(out.positions.at("lfo").x, out.positions.at("m2").x);
    EXPECT_EQ(computeHierarchicalArrange(in).positions, out.positions);
}

// A block with a real signal output keeps its row even when it also modulates another row.
TEST(HierarchicalArrange, ABlockWithASignalOutputKeepsItsRowWhenItAlsoModulatesAnotherRow) {
    ArrangeInput in;
    in.blocks = {module("in1", 280, 200, 1), module("x", 280, 300, 2, 3), card("m1", 280, 90, 3),
                 module("in2", 280, 200, 4), card("m2", 280, 90, 5)};
    in.edges = {flow("in1", "x"), flow("x", "m1"), flow("in2", "m2"), mod("x", "m2")};
    in.trackStarts = {"in1", "in2"};

    const auto out = computeHierarchicalArrange(in);

    EXPECT_LT(out.positions.at("x").y, out.positions.at("in2").y) << "still in track 1's row";
}

// Columns are shared by every row, so a wide block in another row's cell of the same column must not leave a gap
// between a pulled-up source and its consumer: the source hugs the right edge of its column.
TEST(HierarchicalArrange, APulledUpSourceHugsTheRightEdgeOfAWideColumn) {
    ArrangeInput in;
    in.blocks = {module("in1", 280, 200, 1), module("m1", 280, 300, 2, 1), module("in2", 2000, 200, 3),
                 module("m2", 280, 300, 4, 1), module("lfo", 280, 300, 100, 3)};
    in.edges = {flow("in1", "m1"), flow("in2", "m2"), mod("lfo", "m1")};
    in.trackStarts = {"in1", "in2"};

    const auto out = computeHierarchicalArrange(in);

    const auto lfo = out.positions.at("lfo");
    const auto m1 = out.positions.at("m1");
    EXPECT_EQ(out.positions.at("in1").x, kArrangeOriginX) << "the row start stays left-aligned";
    EXPECT_LT(lfo.x + 280, m1.x) << "still before its consumer";
    EXPECT_GE(lfo.x + 280 + kLayerGapX, m1.x - 20) << "no gap the width of the other row's wide block";
    expectTopLevelClear(in, out);
    EXPECT_EQ(computeHierarchicalArrange(in).positions, out.positions);
}
