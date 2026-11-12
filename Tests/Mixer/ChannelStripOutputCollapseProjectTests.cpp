// Regression test for FRO1232: a saved multi-track project (the shared
// Tests/fixtures/saved-project fixture: three track strips in channel macros, one folded, one nested) with a bus and a
// send added. Collapsing every strip's Dual I/O, saving and reopening the project must leave every raw edge exactly
// where it was (kind jack-raw-mismatch): the output collapse only changes which visible jack a raw channel shows on.

#include "../fixtures/saved-project/SavedProjectFixture.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

#include <set>
#include <tuple>

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
using Edge = std::tuple<juce::String, int, juce::String, int>;
constexpr int kR = ChannelStripModule::kRightBase;

juce::String uuidOf(juce::AudioProcessorGraph::Node* node) { return synth::AIStateMapper::ensureNodeUuid(node); }

/** Every audio edge by node uuid (node ids are reassigned on load). */
std::set<Edge> rawEdges(MainComponent& mc) {
    auto& graph = mc.getAudioEngine().getGraph();
    std::set<Edge> out;
    for (const auto& c : graph.getConnections())
        if (!c.source.isMIDI() && !c.destination.isMIDI())
            out.insert({uuidOf(graph.getNodeForId(c.source.nodeID)), c.source.channelIndex,
                        uuidOf(graph.getNodeForId(c.destination.nodeID)), c.destination.channelIndex});
    return out;
}

std::vector<juce::AudioProcessorGraph::Node*> stripNodes(MainComponent& mc) {
    std::vector<juce::AudioProcessorGraph::Node*> out;
    for (auto* node : mc.getAudioEngine().getGraph().getNodes())
        if (dynamic_cast<ChannelStripModule*>(node->getProcessor()) != nullptr)
            out.push_back(node);
    return out;
}

void setDual(MainComponent& mc, bool dual) {
    for (auto* node : stripNodes(mc)) {
        auto* strip = dynamic_cast<ChannelStripModule*>(node->getProcessor());
        for (auto* p : strip->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
                ranged && ranged->getParameterID() == "dualIO")
                ranged->setValueNotifyingHost(dual ? 1.0f : 0.0f);
        for (auto* card : mc.getGraphEditor().getModuleComponents())
            if (card != nullptr && card->getNodeId() == node->nodeID)
                card->applyDualIOLayoutChange();
    }
}

// The strips whose main output feeds a cable: each of those raw channels must sit under a visible jack.
void expectEveryStripCableUnderAVisibleJack(MainComponent& mc) {
    auto& graph = mc.getAudioEngine().getGraph();
    int checked = 0;
    for (const auto& c : graph.getConnections()) {
        auto* node = graph.getNodeForId(c.source.nodeID);
        auto* strip = node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
        if (strip == nullptr || c.source.isMIDI() || c.destination.isMIDI())
            continue;
        ++checked;
        EXPECT_TRUE(GraphEditor::audioChannelReachableFromJack(*strip, c.source.channelIndex, /*isInput=*/false))
            << "strip output channel " << c.source.channelIndex << " has no visible jack";
    }
    EXPECT_GT(checked, 0);
}
} // namespace

TEST(ChannelStripOutputCollapseProject, CollapsingEveryStripOfASavedProjectMovesNoRawEdgeAcrossASaveAndReopen) {
    synth::test::SavedProjectFixture project;
    ASSERT_TRUE(project.wasSaved());
    auto mc = project.open();
    ASSERT_NE(mc, nullptr);
    auto& graph = mc->getAudioEngine().getGraph();

    // A bus, and a send from the first track's strip into it (slot 0 -> bus), on top of the track-to-Master cables.
    const auto busChannel = synth::buildBusChannel(graph, synth::DefaultChannelLayout{});
    ASSERT_NE(busChannel.strip, nullptr);
    const auto bus = busChannel.strip->nodeID;
    NodeID source;
    for (auto* node : stripNodes(*mc))
        if (node->nodeID != bus && source == NodeID{} && !synth::isBusStrip(graph, node->nodeID))
            source = node->nodeID;
    ASSERT_NE(source, NodeID{});
    ASSERT_EQ(synth::addSend(graph, source, bus), 0);
    mc->getGraphEditor().updateComponents();

    setDual(*mc, true);
    const auto split = rawEdges(*mc);
    ASSERT_GT(split.size(), 8u);

    setDual(*mc, false);
    EXPECT_EQ(rawEdges(*mc), split) << "collapsing the strips changed a raw edge";
    expectEveryStripCableUnderAVisibleJack(*mc);
    auto* sourceStrip = dynamic_cast<ChannelStripModule*>(graph.getNodeForId(source)->getProcessor());
    EXPECT_EQ(sourceStrip->getVisibleOutputPortCount(), 1 + 2) << "one Audio jack and the one send pair";

    // Save the collapsed project over the fixture's bundle and open it the way the app opens a file.
    ASSERT_TRUE(mc->saveProjectForTest(project.bundle()));
    auto reopened = project.open();
    ASSERT_NE(reopened, nullptr);
    EXPECT_EQ(rawEdges(*reopened), split) << "a saved collapsed project reopened with a different raw edge";
    ASSERT_FALSE(stripNodes(*reopened).empty());
    for (auto* node : stripNodes(*reopened))
        EXPECT_FALSE(dynamic_cast<ChannelStripModule*>(node->getProcessor())->isDualIO());
    expectEveryStripCableUnderAVisibleJack(*reopened);

    // And back to split on the reopened project: the same edges again.
    setDual(*reopened, true);
    EXPECT_EQ(rawEdges(*reopened), split);
}
