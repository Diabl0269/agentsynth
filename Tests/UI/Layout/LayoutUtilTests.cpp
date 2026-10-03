#include "Modules/AttenuverterModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Layout/LayoutUtil.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

// ============================================================================
// SnapRoundsToNearestGridMultiple
// ============================================================================

TEST(LayoutUtilTest, SnapRoundsToNearestGridMultiple) {
    using namespace synth::LayoutUtil;

    // Exact multiples stay put
    EXPECT_EQ(snap(0), 0);
    EXPECT_EQ(snap(8), 8);
    EXPECT_EQ(snap(16), 16);
    EXPECT_EQ(snap(80), 80);

    // Values below midpoint round down
    EXPECT_EQ(snap(3), 0);

    // Midpoint rounds up (std::lround rounds half away from zero)
    EXPECT_EQ(snap(4), 8);

    // Values above midpoint round up
    EXPECT_EQ(snap(5), 8);
    EXPECT_EQ(snap(12), 16);

    // Negative-safe
    EXPECT_EQ(snap(-4), -8); // -4 is the midpoint between -8 and 0 — rounds away from zero -> -8
    EXPECT_EQ(snap(-3), 0);
    EXPECT_EQ(snap(-5), -8);

    // Point overload
    auto p = snap(juce::Point<int>(3, 5));
    EXPECT_EQ(p.x, 0);
    EXPECT_EQ(p.y, 8);
}

// ============================================================================
// IntersectsAnyRespectsGap
// ============================================================================

TEST(LayoutUtilTest, IntersectsAnyRespectsGap) {
    using namespace synth::LayoutUtil;

    // Two modules: A at (0,0,100,100), B at (110,0,100,100) — distance = 10px
    juce::AudioProcessorGraph::NodeID idA{1}, idB{2}, idC{3};
    juce::Rectangle<int> rectA{0, 0, 100, 100};
    juce::Rectangle<int> rectB{110, 0, 100, 100};

    std::vector<Box> others = {{idB, rectB}};

    // gap=12: A right edge = 100, B left edge = 110, gap = 10 < 12 => intersects
    EXPECT_TRUE(intersectsAny(rectA, others, idA, 12));

    // gap=0: pure rect overlap; they don't overlap (space between them) => false
    EXPECT_FALSE(intersectsAny(rectA, others, idA, 0));

    // selfId excluded: if candidate's own id is in others, it should not self-collide
    std::vector<Box> withSelf = {{idA, rectA}, {idB, rectB}};
    // With gap=12, B still intersects A
    EXPECT_TRUE(intersectsAny(rectA, withSelf, idA, 12));
    // But candidate A vs others where only A is in others (selfId excludes it) => false
    std::vector<Box> onlySelf = {{idA, rectA}};
    EXPECT_FALSE(intersectsAny(rectA, onlySelf, idA, 12));

    // Completely non-overlapping with large gap clearance
    juce::Rectangle<int> rectFar{500, 500, 100, 100};
    EXPECT_FALSE(intersectsAny(rectFar, others, idC, 12));
}

// ============================================================================
// FindFreeSlotReturnsDesiredWhenClear
// ============================================================================

TEST(LayoutUtilTest, FindFreeSlotReturnsDesiredWhenClear) {
    using namespace synth::LayoutUtil;

    juce::AudioProcessorGraph::NodeID selfId{42};
    std::vector<Box> emptyOthers;

    // Desired position already on-grid and clear -> returns snapped desired
    auto result = findFreeSlot({80, 80}, 280, 300, emptyOthers, selfId);
    EXPECT_EQ(result.x % kGridSize, 0) << "Result must be on-grid (x)";
    EXPECT_EQ(result.y % kGridSize, 0) << "Result must be on-grid (y)";
    EXPECT_EQ(result.x, 80);
    EXPECT_EQ(result.y, 80);

    // Non-grid desired snaps, then returns (no collisions)
    auto result2 = findFreeSlot({83, 77}, 280, 300, emptyOthers, selfId);
    EXPECT_EQ(result2.x % kGridSize, 0);
    EXPECT_EQ(result2.y % kGridSize, 0);
    // 83 snaps to 80, 77 snaps to 80
    EXPECT_EQ(result2.x, 80);
    EXPECT_EQ(result2.y, 80);
}

// ============================================================================
// FindFreeSlotResolvesDenseCluster
// ============================================================================

TEST(LayoutUtilTest, FindFreeSlotResolvesDenseCluster) {
    using namespace synth::LayoutUtil;

    // Pack a 3x3 cluster of 280x300 modules starting at (0,0)
    // with kCollisionGap=12, each module occupies (280+12)x(300+12) = 292x312 effective
    std::vector<Box> cluster;
    juce::AudioProcessorGraph::NodeID nextId{1};
    const int w = 280, h = 300;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            juce::AudioProcessorGraph::NodeID id{nextId.uid++};
            cluster.push_back({id, {col * (w + kCollisionGap + 1), row * (h + kCollisionGap + 1), w, h}});
        }
    }

    juce::AudioProcessorGraph::NodeID selfId{99};
    // Drop into the center of the cluster
    auto result = findFreeSlot({280, 300}, w, h, cluster, selfId);

    // Result must be on-grid
    EXPECT_EQ(result.x % kGridSize, 0) << "Result x must be on-grid";
    EXPECT_EQ(result.y % kGridSize, 0) << "Result y must be on-grid";

    // Result must not intersect any occupied box
    juce::Rectangle<int> placed{result.x, result.y, w, h};
    EXPECT_FALSE(intersectsAny(placed, cluster, selfId, kCollisionGap))
        << "Resolved slot must not overlap any cluster member (gap=" << kCollisionGap << ")";

    // Result must be within canvas bounds
    EXPECT_GE(result.x, 0);
    EXPECT_GE(result.y, 0);
}

TEST(LayoutUtilTest, FindFreeSlotFarRightIsNotClamped) {
    using namespace synth::LayoutUtil;
    const auto result = findFreeSlot({20000, 400}, 200, 150, {}, juce::AudioProcessorGraph::NodeID{1});
    EXPECT_EQ(result.x, 20000);
    EXPECT_EQ(result.y, 400);
}

// ============================================================================
// CollisionGapIsEnforcedOnce (regression: VCA column "doesn't fit")
// ============================================================================

// Presets place columns ~20px apart. intersectsAny must enforce kCollisionGap (12px) ONCE, not doubled
// to 24px — otherwise a module ~20px from its neighbour is wrongly flagged as overlapping, so the drag
// ghost bumps it to another column and it "can't fit" in its own slot.
TEST(LayoutUtilTest, CollisionGapIsEnforcedOnce) {
    using namespace synth::LayoutUtil;
    juce::AudioProcessorGraph::NodeID self{1}, neighbour{2};

    juce::Rectangle<int> filter{648, 8, 280, 568}; // right edge 928 (on-grid sample)
    juce::Rectangle<int> vca{952, 8, 280, 200};    // left edge 952 → 24px gap to filter
    std::vector<Box> others = {{neighbour, filter}};

    // 24px gap must NOT be a collision at kCollisionGap=12 (it would be if both boxes were inflated).
    EXPECT_FALSE(intersectsAny(vca, others, self, kCollisionGap))
        << "A 24px column gap must not register as a collision at kCollisionGap=12";

    // A box only 6px from the neighbour (< kCollisionGap) must still register as a collision.
    juce::Rectangle<int> tooClose{934, 8, 280, 200}; // left edge 934 → 6px gap to filter (<12)
    EXPECT_TRUE(intersectsAny(tooClose, others, self, kCollisionGap))
        << "A 6px gap (< kCollisionGap) must register as a collision";

    // findFreeSlot must leave the 24px-gap box exactly where it is (snapped), not bump it to another column.
    auto placed = findFreeSlot({vca.getX(), vca.getY()}, vca.getWidth(), vca.getHeight(), others, self);
    EXPECT_EQ(placed.x, snap(vca.getX())) << "VCA must stay in its column, not get bumped";
    EXPECT_EQ(placed.y, snap(vca.getY()));
}

// ============================================================================
// WidthBucket_MappingTable
// ============================================================================

TEST(LayoutUtilTest, WidthBucket_MappingTable) {
    using namespace synth::LayoutUtil;

    // Double-width modules (wide, interactive)
    EXPECT_EQ(getModuleWidthBucket(ModuleType::Sequencer), ModuleWidthBucket::Double);
    EXPECT_EQ(getModuleWidthBucket(ModuleType::PolySequencer), ModuleWidthBucket::Double);
    EXPECT_EQ(getModuleWidthBucket(ModuleType::MidiKeyboard), ModuleWidthBucket::Double);
    // Parametric EQ needs the width for its response curve plus a 4-column band grid.
    EXPECT_EQ(getModuleWidthBucket(ModuleType::ParametricEQ), ModuleWidthBucket::Double);

    // Narrow-width module (attenuverter)
    EXPECT_EQ(getModuleWidthBucket(ModuleType::Attenuverter), ModuleWidthBucket::Narrow);

    // Single-width standard modules
    EXPECT_EQ(getModuleWidthBucket(ModuleType::Oscillator), ModuleWidthBucket::Single);
    EXPECT_EQ(getModuleWidthBucket(ModuleType::Filter), ModuleWidthBucket::Single);
    EXPECT_EQ(getModuleWidthBucket(ModuleType::VCA), ModuleWidthBucket::Single);
    EXPECT_EQ(getModuleWidthBucket(ModuleType::ADSR), ModuleWidthBucket::Single);
    EXPECT_EQ(getModuleWidthBucket(ModuleType::LFO), ModuleWidthBucket::Single);
    EXPECT_EQ(getModuleWidthBucket(ModuleType::VoiceMixer), ModuleWidthBucket::Single);

    // FX module (representative)
    EXPECT_EQ(getModuleWidthBucket(ModuleType::Distortion), ModuleWidthBucket::Single);
}

// ============================================================================
// WidthBucket_ConstantsOnGrid
// ============================================================================

TEST(LayoutUtilTest, WidthBucket_ConstantsOnGrid) {
    using namespace synth::LayoutUtil;

    // All width constants are multiples of the grid size
    EXPECT_EQ(kNarrowWidth % kGridSize, 0);
    EXPECT_EQ(kSingleWidth % kGridSize, 0);
    EXPECT_EQ(kDoubleWidth % kGridSize, 0);

    // Double width is exactly 2x single width
    EXPECT_EQ(kDoubleWidth, 2 * kSingleWidth);

    // moduleWidth(bucket) returns the correct constants
    EXPECT_EQ(moduleWidth(ModuleWidthBucket::Narrow), kNarrowWidth);
    EXPECT_EQ(moduleWidth(ModuleWidthBucket::Single), kSingleWidth);
    EXPECT_EQ(moduleWidth(ModuleWidthBucket::Double), kDoubleWidth);
}

// ============================================================================
// WidthBucket_ColumnStride
// ============================================================================

TEST(LayoutUtilTest, WidthBucket_ColumnStride) {
    using namespace synth::LayoutUtil;

    // Canonical auto-arrange column pitch: kSingleWidth + kLayerGapX = 360px
    EXPECT_EQ(kSingleWidth + kLayerGapX, 360);
}

// ============================================================================
// findFreeSlotBelow: keeps the desired x, walks down in grid steps
// ============================================================================

TEST(LayoutUtilTest, FindFreeSlotBelowKeepsXAndTakesTheFirstClearRow) {
    using namespace synth::LayoutUtil;
    const std::vector<Box> others{{NodeID(1), {0, 0, 280, 200}}, {NodeID(2), {0, 240, 280, 100}}};

    // Clear already: the desired spot, snapped.
    EXPECT_EQ(findFreeSlotBelow({400, 40}, 280, 100, others), juce::Point<int>(400, 40));

    // Blocked: same x, and the first grid row clearing every box by the collision gap.
    const auto spot = findFreeSlotBelow({0, 0}, 280, 100, others);
    EXPECT_EQ(spot.x, 0);
    EXPECT_GE(spot.y, 340 + kCollisionGap);
    EXPECT_LT(spot.y, 340 + kCollisionGap + kGridSize);
    EXPECT_EQ(spot.y % kGridSize, 0);
    EXPECT_FALSE(intersectsAny({spot.x, spot.y, 280, 100}, others, NodeID{}));
}

TEST(LayoutUtilTest, FindFreeSlotBelowClampsToTheCanvasOrigin) {
    using namespace synth::LayoutUtil;
    EXPECT_EQ(findFreeSlotBelow({-300, -16}, 280, 100, {}), juce::Point<int>(0, 0));
}

// ============================================================================
// MacroBankGeometry
// ============================================================================

TEST(LayoutUtilTest, MacroBank_HeightGrowsOneRowPerMacro) {
    using namespace synth::LayoutUtil;

    EXPECT_EQ(macroBankHeight(4), kMacroHeaderH + 4 * kMacroRowH + kMacroBottomPad);
    EXPECT_EQ(macroBankHeight(8) - macroBankHeight(4), 4 * kMacroRowH);
    EXPECT_EQ(macroBankHeight(16) - macroBankHeight(8), 8 * kMacroRowH);
    EXPECT_GT(macroBankHeight(1), kMacroHeaderH);
}

TEST(LayoutUtilTest, MacroBank_RowCentresAreEvenlySpacedAndInsideTheBank) {
    using namespace synth::LayoutUtil;

    EXPECT_EQ(macroRowCentreY(1) - macroRowCentreY(0), kMacroRowH);
    EXPECT_GT(macroRowCentreY(0), kMacroHeaderH);

    // Every visible row's jack must sit inside the component it belongs to.
    for (int count = 1; count <= 16; ++count)
        EXPECT_LT(macroRowCentreY(count - 1), macroBankHeight(count)) << "count " << count;
}
