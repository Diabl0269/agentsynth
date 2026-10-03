// ChannelFlowProjectEditPlacementTests.cpp
//
// Where an edit plan's additions land, applied through the REAL app host (MainComponentTimelineOpsHost) and the
// real applyProjectEdit, which detaches every card before the batch (docs/ai/timeline-ops.md#where-things-land):
// a plan-built track below every existing track macro, and patch nodes with no "position" beside what they
// connect to. The walk-down itself is covered headless in Tests/UI/Layout/LayoutUtilTests.cpp.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "ChannelFlowTestFixture.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"
#include <gtest/gtest.h>
#include <set>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

const synth::Macro* macroNamedPL(MainComponent& mc, const juce::String& name) {
    for (const auto& macro : mc.getGraphEditor().getMacros().getAll())
        if (macro.name == name)
            return &macro;
    return nullptr;
}

// What a macro takes up on the canvas: its open hull, or its collapsed card.
juce::Rectangle<int> footprintPL(MainComponent& mc, const juce::String& macroId) {
    auto& editor = mc.getGraphEditor();
    if (const auto hull = editor.getMacroController().macroHullBounds(macroId); !hull.isEmpty())
        return hull;
    const auto* macro = editor.getMacros().find(macroId);
    return macro != nullptr ? macro->bounds : juce::Rectangle<int>();
}

juce::Rectangle<int> cardOfPL(MainComponent& mc, NodeID id) {
    auto* comp = compForCFT(mc.getGraphEditor(), id);
    return comp != nullptr ? comp->getBounds() : juce::Rectangle<int>();
}

// Every visible card other than `id` that `id`'s card overlaps.
int overlapsPL(MainComponent& mc, NodeID id) {
    const auto mine = cardOfPL(mc, id);
    int count = 0;
    for (auto* comp : mc.getGraphEditor().getModuleComponents())
        if (comp != nullptr && comp->getNodeId() != id && comp->isVisible() && comp->getBounds().intersects(mine))
            ++count;
    return count;
}

NodeID newNodeOfTypePL(juce::AudioProcessorGraph& graph, const juce::String& type, const std::set<NodeID>& before) {
    for (auto* node : graph.getNodes())
        if (before.count(node->nodeID) == 0 && synth::AIStateMapper::getFactoryTypeName(node->getProcessor()) == type)
            return node->nodeID;
    return {};
}

std::set<NodeID> liveIdsPL(juce::AudioProcessorGraph& graph) {
    std::set<NodeID> ids;
    for (auto* node : graph.getNodes())
        ids.insert(node->nodeID);
    return ids;
}

// Applies `json` as one edit plan, then runs the card rebuild aiPatchApplied posts asynchronously.
void applyPlanPL(MainComponent& mc, const juce::String& json) {
    const auto applied = mc.getAiServiceForTest().applyProjectEdit(juce::JSON::parse(json));
    ASSERT_TRUE(applied.ok) << applied.message;
    mc.getGraphEditor().updateComponents();
}

void makeMainComponentQuiet(MainComponent& mc) {
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
}

} // namespace

TEST_F(ChannelFlowTest, PlanBuiltTrackLandsBelowEveryExistingTrackMacro) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    makeMainComponentQuiet(mc);
    addInstrumentTrack(mc, "Oscillator");
    addInstrumentTrack(mc, "Oscillator");
    mc.getGraphEditor().updateComponents();
    std::vector<juce::String> existing;
    for (const auto& macro : mc.getGraphEditor().getMacros().getAll())
        existing.push_back(macro.id);
    ASSERT_EQ(existing.size(), 2u);
    // Open both: the hulls are what a new track must clear, and they are much larger than the collapsed cards.
    for (const auto& id : existing)
        mc.getGraphEditor().getMacroController().setMacroCollapsed(id, false);
    mc.getGraphEditor().updateComponents();
    int lowest = 0;
    for (const auto& id : existing)
        lowest = juce::jmax(lowest, footprintPL(mc, id).getBottom());

    applyPlanPL(mc, R"({"timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator"}]})");

    const auto* bass = macroNamedPL(mc, "Bass");
    ASSERT_NE(bass, nullptr);
    const auto bassFootprint = footprintPL(mc, bass->id);
    ASSERT_FALSE(bassFootprint.isEmpty());
    EXPECT_GE(bassFootprint.getY(), lowest) << "below the lowest existing track macro";
    for (const auto& id : existing)
        EXPECT_FALSE(footprintPL(mc, id).intersects(bassFootprint)) << "no overlap with " << id;
}

TEST_F(ChannelFlowTest, PlanLfoModulatingAnExistingFilterLandsLeftOfIt) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    makeMainComponentQuiet(mc);
    auto& graph = mc.getAudioEngine().getGraph();
    auto filter = graph.addNode(synth::AIStateMapper::createModule("Filter"));
    ASSERT_NE(filter, nullptr);
    // Clear of the default patch, so the row left of it is free.
    filter->properties.set("x", 2400);
    filter->properties.set("y", 1600);
    mc.getGraphEditor().updateComponents();
    const auto before = liveIdsPL(graph);
    const auto filterCard = cardOfPL(mc, filter->nodeID);

    applyPlanPL(mc, R"({"mode": "merge", "nodes": [{"id": 9001, "type": "LFO"}], "modulations": [
        {"source": 9001, "dest": )" +
                        juce::String(filter->nodeID.uid) + R"(, "destParam": "cutoff", "amount": 0.5}]})");

    const auto lfo = newNodeOfTypePL(graph, "LFO", before);
    ASSERT_NE(lfo.uid, 0u);
    const auto lfoCard = cardOfPL(mc, lfo);
    EXPECT_LE(lfoCard.getRight(), filterCard.getX()) << "left of the Filter it modulates";
    EXPECT_GE(lfoCard.getY(), filterCard.getY()) << "on the Filter's row, or walked down from it";
    EXPECT_EQ(lfoCard.getY(), filterCard.getY()) << "the row left of the Filter is free here";
    EXPECT_EQ(lfoCard.getRight() + synth::LayoutUtil::kLayerGapX, filterCard.getX()) << "one column gap away";
    EXPECT_EQ(overlapsPL(mc, lfo), 0);
}

TEST_F(ChannelFlowTest, PlanChainWithNoPositionsLaysOutLeftToRight) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    makeMainComponentQuiet(mc);
    auto& graph = mc.getAudioEngine().getGraph();
    const auto before = liveIdsPL(graph);

    applyPlanPL(mc, R"({"mode": "merge",
        "nodes": [{"id": 9101, "type": "Oscillator"}, {"id": 9102, "type": "Filter"}, {"id": 9103, "type": "VCA"}],
        "connections": [{"src": 9101, "srcPort": 0, "dst": 9102, "dstPort": 0},
                        {"src": 9102, "srcPort": 0, "dst": 9103, "dstPort": 0}]})");

    const auto osc = cardOfPL(mc, newNodeOfTypePL(graph, "Oscillator", before));
    const auto filter = cardOfPL(mc, newNodeOfTypePL(graph, "Filter", before));
    const auto vca = cardOfPL(mc, newNodeOfTypePL(graph, "VCA", before));
    ASSERT_FALSE(osc.isEmpty() || filter.isEmpty() || vca.isEmpty());
    EXPECT_EQ(osc.getRight() + synth::LayoutUtil::kLayerGapX, filter.getX()) << "Filter right of Osc";
    EXPECT_EQ(filter.getRight() + synth::LayoutUtil::kLayerGapX, vca.getX()) << "VCA right of Filter";
    EXPECT_EQ(filter.getY(), osc.getY()) << "one row";
    EXPECT_EQ(vca.getY(), filter.getY()) << "one row";
    for (const auto& type : {"Oscillator", "Filter", "VCA"})
        EXPECT_EQ(overlapsPL(mc, newNodeOfTypePL(graph, type, before)), 0) << type;
}

TEST_F(ChannelFlowTest, PlanNodeAnchoredOnAMacroMemberJoinsThatMacroInsideItsOutline) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    makeMainComponentQuiet(mc);
    auto& graph = mc.getAudioEngine().getGraph();
    applyPlanPL(mc, R"({"timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
        "inserts": [{"type": "Filter"}]}]})");
    const auto* bass = macroNamedPL(mc, "Bass");
    ASSERT_NE(bass, nullptr);
    const juce::String bassId = bass->id;
    mc.getGraphEditor().getMacroController().setMacroCollapsed(bassId, false);
    mc.getGraphEditor().updateComponents();
    auto* filter = findMacroMemberOfTypeCFT(graph, *mc.getGraphEditor().getMacros().find(bassId), ModuleType::Filter);
    ASSERT_NE(filter, nullptr);
    const auto filterId = filter->nodeID;
    const auto ids = liveIdsPL(graph);
    const auto before = snapshotCFT(mc);

    applyPlanPL(mc, R"({"mode": "merge", "nodes": [{"id": 9201, "type": "LFO"}], "modulations": [
        {"source": 9201, "dest": )" +
                        juce::String(filterId.uid) + R"(, "destParam": "cutoff", "amount": 0.5}]})");

    const auto lfo = newNodeOfTypePL(graph, "LFO", ids);
    ASSERT_NE(lfo.uid, 0u);
    const auto* macro = mc.getGraphEditor().getMacros().find(bassId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(graph.getNodeForId(lfo)->properties["uuid"].toString())) << "joined the macro";
    const auto lfoCard = cardOfPL(mc, lfo);
    EXPECT_TRUE(mc.getGraphEditor().getMacroController().macroHullBounds(bassId).contains(lfoCard))
        << "inside the macro's outline";
    EXPECT_LE(lfoCard.getRight(), cardOfPL(mc, filterId).getX()) << "left of the Filter it modulates";
    EXPECT_EQ(overlapsPL(mc, lfo), 0);

    // The spot, the join and whatever the macro pushed are the plan's one undo step.
    expectOneUndoStepCFT(mc, before);
}
