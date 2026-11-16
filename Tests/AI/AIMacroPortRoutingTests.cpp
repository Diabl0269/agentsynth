// AIMacroPortRoutingTests.cpp
// An AI patch that wires a cable into a macro member enters through a macro port, like a dragged cable
// (docs/macros/auto-ports.md#programmatic-connections); a trusted apply (load, undo, snippet paste) never does.

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/MacroInletModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;

class AIMacroPortRouting : public ::testing::Test {
protected:
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    synth::AIIntegrationService service{engine.getGraph(), &undo};
    NodeID filterId{};
    juce::String macroId;
    juce::String mergePatch;

    void SetUp() override {
        editor.setSize(1600, 1200);
        auto& graph = engine.getGraph();
        graph.clear();
        const auto baseline = juce::JSON::parse(
            R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Filter"},{"id":3,"type":"VCA"},)"
            R"({"id":4,"type":"Audio Output"}],"connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0},)"
            R"({"src":2,"srcPort":0,"dst":3,"dstPort":0},{"src":3,"srcPort":0,"dst":4,"dstPort":0}]})");
        ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(baseline, graph, true));
        editor.updateComponents();

        std::vector<NodeID> members;
        for (auto* node : graph.getNodes()) {
            const auto name = node->getProcessor()->getName();
            if (name == "Filter" || name == "VCA")
                members.push_back(node->nodeID);
            if (name == "Filter")
                filterId = node->nodeID;
        }
        editor.setSelectedNodes(members);
        macroId = editor.getMacroController().groupSelectionIntoMacro();
        ASSERT_FALSE(macroId.isEmpty());
        mergePatch = R"({"mode":"merge","nodes":[{"id":9001,"type":"LFO"}],"modulations":[{"source":9001,"dest":)" +
                     juce::String((int)filterId.uid) + R"(,"destParam":"cutoff"}]})";
        undo.clearUndoHistory(); // the grouping above is setup, not part of what a test undoes
    }

    void installRouting() {
        service.setMacroRouting(&editor.getMacros(), [this](const std::function<bool()>& mutation) {
            return editor.getMacroController().applyProgrammaticConnectionChange(
                editor.getAutoCreateMacroPortsOnDragEnabled(), mutation);
        });
    }

    int inletCount() {
        int n = 0;
        for (auto* node : engine.getGraph().getNodes())
            n += dynamic_cast<MacroInletModule*>(node->getProcessor()) != nullptr ? 1 : 0;
        return n;
    }
    size_t portCount() { return editor.getMacros().find(macroId)->ports.size(); }
    /** True when a macro inlet carries a cable in (from outside) and a cable on (into the macro). */
    bool anInletIsWiredBothSides() {
        for (const auto& c : engine.getGraph().getConnections()) {
            auto* dst = engine.getGraph().getNodeForId(c.destination.nodeID);
            if (dst == nullptr || dynamic_cast<MacroInletModule*>(dst->getProcessor()) == nullptr)
                continue;
            for (const auto& out : engine.getGraph().getConnections())
                if (out.source.nodeID == c.destination.nodeID)
                    return true;
        }
        return false;
    }
};
} // namespace

// Regression test for FRO359: an AI patch's cable into a macro member was wired straight through the macro boundary.
TEST_F(AIMacroPortRouting, ModulationIntoAMemberEntersThroughANewInlet) {
    installRouting();
    ASSERT_TRUE(service.applyPatch(mergePatch, /*mergeMode=*/true));
    EXPECT_EQ(portCount(), 1u);
    EXPECT_EQ(inletCount(), 1);
    EXPECT_TRUE(anInletIsWiredBothSides());
}

// Regression test for FRO359: with the preference off the patch is wired straight, as before.
TEST_F(AIMacroPortRouting, PreferenceOffWiresStraight) {
    editor.setAutoCreateMacroPortsOnDragEnabled(false);
    installRouting();
    ASSERT_TRUE(service.applyPatch(mergePatch, /*mergeMode=*/true));
    EXPECT_EQ(portCount(), 0u);
    EXPECT_EQ(inletCount(), 0);
}

// Regression test for FRO359: the ports an AI patch mints undo and redo with the graph, as one step.
TEST_F(AIMacroPortRouting, UndoAndRedoRestoreGraphAndMacrosTogether) {
    installRouting();
    const int nodesBefore = engine.getGraph().getNumNodes();
    ASSERT_TRUE(service.applyPatch(mergePatch, /*mergeMode=*/true));
    const int nodesAfter = engine.getGraph().getNumNodes();
    ASSERT_EQ(portCount(), 1u);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(engine.getGraph().getNumNodes(), nodesBefore);
    EXPECT_EQ(portCount(), 0u);
    EXPECT_FALSE(undo.canUndo()) << "the apply and its ports were ONE step";

    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(engine.getGraph().getNumNodes(), nodesAfter);
    EXPECT_EQ(portCount(), 1u);
    EXPECT_EQ(inletCount(), 1);
}

// Regression test for FRO359: a trusted apply (load, snippet paste) never routes, even with a router installed.
TEST_F(AIMacroPortRouting, TrustedApplyLeavesMacrosAsSaved) {
    installRouting();
    auto state = synth::AIStateMapper::graphToJSON(engine.getGraph());
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(state, engine.getGraph(), true, /*trusted=*/true));
    EXPECT_EQ(portCount(), 0u);
    EXPECT_EQ(inletCount(), 0);
}

// Regression test for FRO359: with no router (tests, plugin) the apply is the direct one it always was.
TEST_F(AIMacroPortRouting, NoRouterAppliesDirectly) {
    ASSERT_TRUE(service.applyPatch(mergePatch, /*mergeMode=*/true));
    EXPECT_EQ(portCount(), 0u);
    EXPECT_EQ(inletCount(), 0);
    ASSERT_TRUE(undo.undo());
}
