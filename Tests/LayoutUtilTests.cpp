#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/UI/LayoutUtil.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

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
    EXPECT_LE(result.x, kCanvasMax - w);
    EXPECT_LE(result.y, kCanvasMax - h);
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
// ComputeAutoArrangeLayersBySignalDepth
// ============================================================================

TEST(LayoutUtilTest, ComputeAutoArrangeLayersBySignalDepth) {
    using namespace synth::LayoutUtil;

    // Build a real AudioProcessorGraph:
    //   AudioInput -> Oscillator -> Filter -> VCA -> AudioOutput
    //   LFO -> Filter (extraEdge, simulating mod routing)
    juce::AudioProcessorGraph graph;

    // Add IO processors
    graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    // Get actual NodeIDs after insertion
    NodeID audioInId, audioOutId;
    NodeID oscId, filterId, vcaId, lfoId;

    for (auto* node : graph.getNodes()) {
        if (auto* io = dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(node->getProcessor())) {
            if (io->getType() == juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode)
                audioInId = node->nodeID;
            else if (io->getType() == juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode)
                audioOutId = node->nodeID;
        }
    }

    oscId = graph.addNode(std::make_unique<OscillatorModule>())->nodeID;
    filterId = graph.addNode(std::make_unique<FilterModule>())->nodeID;
    vcaId = graph.addNode(std::make_unique<VCAModule>())->nodeID;
    lfoId = graph.addNode(std::make_unique<LFOModule>())->nodeID;

    // Signal chain edges in the graph
    // Module->module audio connections exercise the getConnections() edge path
    // (these succeed because the modules expose matching audio channels).
    graph.addConnection({{oscId, 0}, {filterId, 0}}); // Oscillator -> Filter
    graph.addConnection({{filterId, 0}, {vcaId, 0}}); // Filter -> VCA

    // Edges that touch IO nodes (AudioIn -> Osc, VCA -> AudioOut) cannot be created via
    // addConnection here: the graph has no setPlayConfigDetails, so the IO processors expose
    // 0 channels, and the Oscillator has no audio input. Feed them (plus the LFO mod edge)
    // through extraEdges, which computeAutoArrange treats as topology edges directly.
    std::vector<std::pair<NodeID, NodeID>> extraEdges = {{audioInId, oscId}, {vcaId, audioOutId}, {lfoId, filterId}};

    // sizeOf stub: return fixed 280x300 for all
    auto sizeOf = [](NodeID) -> juce::Point<int> { return {280, 300}; };

    auto results = computeAutoArrange(graph, sizeOf, extraEdges);

    // Must have a result for every non-attenuverter node
    // AudioIn, AudioOut, Osc, Filter, VCA, LFO => 6 nodes
    ASSERT_GE(static_cast<int>(results.size()), 4) << "Must arrange at least the signal-chain modules";

    // Build result map: nodeId -> x position
    std::map<NodeID, juce::Point<int>> posMap;
    for (auto& r : results)
        posMap[r.id] = r.pos;

    // AudioOutput must be in the results
    ASSERT_TRUE(posMap.count(audioOutId)) << "AudioOutput must be in arrange results";

    // Helper: get x for a node (skip if not present)
    auto xOf = [&](NodeID id) -> int {
        auto it = posMap.find(id);
        return (it != posMap.end()) ? it->second.x : -1;
    };

    // Signal chain: AudioIn <= Osc <= Filter <= VCA <= AudioOut
    // x must strictly increase per depth layer
    if (posMap.count(audioInId) && posMap.count(oscId))
        EXPECT_LE(xOf(audioInId), xOf(oscId)) << "AudioInput x must be <= Oscillator x";
    if (posMap.count(oscId) && posMap.count(filterId))
        EXPECT_LT(xOf(oscId), xOf(filterId)) << "Oscillator x must be < Filter x";
    if (posMap.count(filterId) && posMap.count(vcaId))
        EXPECT_LT(xOf(filterId), xOf(vcaId)) << "Filter x must be < VCA x";
    if (posMap.count(vcaId))
        EXPECT_LT(xOf(vcaId), xOf(audioOutId)) << "VCA x must be < AudioOutput x";

    // AudioOutput must be in the last column (no node should have x > audioOut x,
    // unless the result is clamped — check it's at least as far as VCA)
    int audioOutX = xOf(audioOutId);
    if (posMap.count(vcaId))
        EXPECT_GE(audioOutX, xOf(vcaId)) << "AudioOutput must be in last or later column than VCA";

    // All result positions must be on-grid
    for (auto& r : results) {
        EXPECT_EQ(r.pos.x % kGridSize, 0) << "x must be on-grid for node " << r.id.uid;
        EXPECT_EQ(r.pos.y % kGridSize, 0) << "y must be on-grid for node " << r.id.uid;
    }

    // No two result boxes should overlap (with gap=kCollisionGap)
    const int w = 280, h = 300;
    for (size_t i = 0; i < results.size(); ++i) {
        juce::Rectangle<int> ri{results[i].pos.x, results[i].pos.y, w, h};
        for (size_t j = i + 1; j < results.size(); ++j) {
            juce::Rectangle<int> rj{results[j].pos.x, results[j].pos.y, w, h};
            // Inflate both by half-gap and check for intersection
            auto riInflated = ri.expanded(kCollisionGap / 2);
            auto rjInflated = rj.expanded(kCollisionGap / 2);
            EXPECT_FALSE(riInflated.intersects(rjInflated))
                << "Modules " << results[i].id.uid << " and " << results[j].id.uid << " overlap in auto-arrange result";
        }
    }
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

// ============================================================================
// resolveOverlapsAfterResize
// ============================================================================

namespace {
synth::LayoutUtil::Box makeBox(juce::uint32 uid, int x, int y, int w, int h) {
    return {synth::LayoutUtil::NodeID{uid}, {x, y, w, h}};
}
} // namespace

TEST(LayoutUtilTest, ResolveOverlapsAfterResize_NoOverlapMovesNothing) {
    using namespace synth::LayoutUtil;

    std::vector<Box> boxes = {makeBox(1, 0, 0, 280, 300), makeBox(2, 0, 400, 280, 200)};
    EXPECT_TRUE(resolveOverlapsAfterResize(NodeID{1}, boxes).empty());
}

TEST(LayoutUtilTest, ResolveOverlapsAfterResize_PushesTheNeighbourBelowClear) {
    using namespace synth::LayoutUtil;

    // Node 1 has just grown from 300px to 700px tall and now swallows node 2.
    std::vector<Box> boxes = {makeBox(1, 0, 0, 280, 700), makeBox(2, 0, 400, 280, 200)};

    auto moved = resolveOverlapsAfterResize(NodeID{1}, boxes);
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_EQ(moved[0].id, NodeID{2});
    EXPECT_GE(moved[0].pos.y, 700 + kCollisionGap);
    EXPECT_EQ(moved[0].pos.y % kGridSize, 0);
}

TEST(LayoutUtilTest, ResolveOverlapsAfterResize_NeverMovesTheResizedModule) {
    using namespace synth::LayoutUtil;

    std::vector<Box> boxes = {makeBox(1, 100, 100, 280, 700), makeBox(2, 100, 400, 280, 200)};

    for (const auto& m : resolveOverlapsAfterResize(NodeID{1}, boxes))
        EXPECT_NE(m.id, NodeID{1});
}

TEST(LayoutUtilTest, ResolveOverlapsAfterResize_CascadesThroughAStack) {
    using namespace synth::LayoutUtil;

    // Three modules stacked tightly below the one that grows: displacing the first must not
    // simply park it on top of the next one.
    std::vector<Box> boxes = {makeBox(1, 0, 0, 280, 700), makeBox(2, 0, 400, 280, 100), makeBox(3, 0, 520, 280, 100),
                              makeBox(4, 0, 640, 280, 100)};

    auto moved = resolveOverlapsAfterResize(NodeID{1}, boxes);
    ASSERT_FALSE(moved.empty());

    // Apply the moves and verify the whole canvas is overlap-free afterwards.
    std::vector<Box> settled = boxes;
    for (const auto& m : moved)
        for (auto& b : settled)
            if (b.id == m.id)
                b.rect.setPosition(m.pos);

    for (const auto& b : settled)
        EXPECT_FALSE(intersectsAny(b.rect, settled, b.id)) << "box " << b.id.uid << " still overlaps";
}

TEST(LayoutUtilTest, ResolveOverlapsAfterResize_ShrinkingLeavesEveryoneWhereTheyAre) {
    using namespace synth::LayoutUtil;

    // A bank that shrank from 700px to 300px: nothing is in the way any more, so nothing moves
    // back up — the canvas simply gains empty space.
    std::vector<Box> boxes = {makeBox(1, 0, 0, 280, 300), makeBox(2, 0, 800, 280, 200)};
    EXPECT_TRUE(resolveOverlapsAfterResize(NodeID{1}, boxes).empty());
}
