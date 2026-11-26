// A poly voice fan cabled to the graph's Audio Output sums every voice onto the leg it lands on: the link
// resolution, the one cable it draws, its one-step disconnect, the Poly toggle re-wiring an existing Output cable,
// and the delete-heal that lands a poly chain straight on Output.
// The audible proof (a Poly Sequencer chord) is in Tests/Modules/Sequencer/SequencerChordRenderTests.cpp.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/ModuleComponentInternal.h"
#include "UI/Graph/PolyChain/PolyChainController.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using Edges = std::vector<std::pair<int, int>>; // (source raw channel, Output raw channel), sorted

struct OutputRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID osc;
    NodeID out;

    OutputRig() {
        undo.setGraphEditor(&editor);
        editor.setSize(2000, 900);
        editor.getPolyChain().confirmForTest = [](const synth::ui::PolyChainPrompt&,
                                                  std::function<void(synth::ui::PolyChainChoice)> reply) {
            reply(synth::ui::PolyChainChoice::thisTrackOnly);
        };
        out = addAudioOutputNode(engine.getGraph(), 1500, 100)->nodeID;
        osc = addModule(std::make_unique<OscillatorModule>(), 100);
        setDualIOParam(*engine.getGraph().getNodeForId(osc)->getProcessor(), false); // one collapsed Audio jack
    }

    juce::AudioProcessor& oscillator() { return *engine.getGraph().getNodeForId(osc)->getProcessor(); }

    NodeID addModule(std::unique_ptr<juce::AudioProcessor> processor, int x) {
        auto node = engine.getGraph().addNode(std::move(processor));
        node->properties.set("x", x);
        node->properties.set("y", 100);
        node->properties.set("uuid", juce::Uuid().toDashedString());
        editor.updateComponents();
        return node->nodeID;
    }

    Edges edgesFrom(NodeID source) {
        Edges edges;
        for (const auto& c : engine.getGraph().getConnections())
            if (c.source.nodeID == source && c.destination.nodeID == out && !c.source.isMIDI())
                edges.emplace_back(c.source.channelIndex, c.destination.channelIndex);
        std::sort(edges.begin(), edges.end());
        return edges;
    }

    int cablesInto(NodeID dest) {
        int n = 0;
        for (const auto& cable : editor.buildVisibleCables())
            if (cable.id.dstUid == dest.uid)
                ++n;
        return n;
    }

    void cable(NodeID source, int sourceJack, NodeID dest, int destJack) {
        editor.connectPorts(source, sourceJack, dest, destJack, false, /*recordUndo=*/true);
    }
};

Edges eightVoicesOntoLeft() {
    Edges edges;
    for (int v = 0; v < 8; ++v)
        edges.emplace_back(v, 0);
    return edges;
}

Edges monoPair() { return {{0, 0}, {OscillatorModule::kRightBase, 1}}; }

} // namespace

TEST_F(GraphEditorTest, ResolvePolyLinkSumsAPolyFanIntoAudioOutput) {
    OscillatorModule poly;
    setPolyParam(poly, true);

    const auto left = GraphEditor::resolvePolyLink(&poly, 0, nullptr, 0);
    EXPECT_EQ(left.sourceRawChannel, 0);
    EXPECT_EQ(left.destRawChannel, 0);
    EXPECT_EQ(left.voiceCount, 8);
    EXPECT_EQ(left.sourceStride, 1);
    EXPECT_EQ(left.destStride, 0);

    const auto right = GraphEditor::resolvePolyLink(&poly, 0, nullptr, 1);
    EXPECT_EQ(right.destRawChannel, 1);
    EXPECT_EQ(right.voiceCount, 8);
    EXPECT_EQ(right.destStride, 0);

    OscillatorModule mono;
    setDualIOParam(mono, false);
    const auto pair = GraphEditor::resolvePolyLink(&mono, 0, nullptr, 0);
    EXPECT_EQ(pair.voiceCount, 2) << "a mono Oscillator's collapsed jack is still a stereo pair onto Left and Right";
    EXPECT_EQ(pair.destStride, 1);
}

// Regression test for FRO2280: a poly Oscillator cabled to Audio Output kept voice 0 only, so a chord played its root.
TEST_F(GraphEditorTest, PolyFanIntoAudioOutputWiresEveryVoice) {
    OutputRig rig;
    setPolyParam(rig.oscillator(), true);
    rig.cable(rig.osc, 0, rig.out, 0);

    EXPECT_EQ(rig.edgesFrom(rig.osc), eightVoicesOntoLeft());
    EXPECT_EQ(rig.cablesInto(rig.out), 1) << "eight voices are one cable on the canvas";

    OutputRig monoRig;
    monoRig.cable(monoRig.osc, 0, monoRig.out, 0);
    EXPECT_EQ(monoRig.edgesFrom(monoRig.osc), monoPair());
    EXPECT_EQ(monoRig.cablesInto(monoRig.out), 2) << "a mono Oscillator still draws its Left and Right cables";
}

// Regression test for FRO2364: a chord on Left alone played on both sides with nothing on the card saying so.
TEST_F(GraphEditorTest, AudioOutputRightReadsFollowsLeftOnlyWhileLeftAloneIsCabled) {
    OutputRig rig;
    setPolyParam(rig.oscillator(), true);
    const auto rightLabel = [&] {
        rig.engine.refreshNormalling();
        return detail::audioOutputJackLabel(1, rig.engine.isOutputRightNormalledFromLeft());
    };
    EXPECT_EQ(rightLabel(), "Right") << "nothing cabled: nothing to follow";
    rig.cable(rig.osc, 0, rig.out, 0);
    EXPECT_EQ(rightLabel(), "Right (follows L)") << "one cable on Left plays on both sides";
    EXPECT_EQ(detail::audioOutputJackLabel(0, true), "L / Mono");
    rig.cable(rig.osc, 0, rig.out, 1);
    EXPECT_EQ(rightLabel(), "Right") << "a cable on Right stops it following Left";
}

TEST_F(GraphEditorTest, PolyFanIntoAudioOutputDisconnectsAsOneUndoStep) {
    OutputRig rig;
    setPolyParam(rig.oscillator(), true);
    rig.cable(rig.osc, 0, rig.out, 0);
    ASSERT_EQ(rig.edgesFrom(rig.osc).size(), 8u);

    const auto cables = rig.editor.buildVisibleCables();
    const auto drawn =
        std::find_if(cables.begin(), cables.end(), [&](const auto& c) { return c.id.dstUid == rig.out.uid; });
    ASSERT_NE(drawn, cables.end());
    rig.editor.disconnectCable(*drawn);
    EXPECT_TRUE(rig.edgesFrom(rig.osc).empty()) << "every voice goes with the cable";

    ASSERT_TRUE(rig.undo.undo());
    EXPECT_EQ(rig.edgesFrom(rig.osc), eightVoicesOntoLeft()) << "one undo brings all eight back";
}

TEST_F(GraphEditorTest, TogglingPolyRewiresAnOutputCableAsOneSummedFan) {
    OutputRig rig;
    rig.cable(rig.osc, 0, rig.out, 0);
    ASSERT_EQ(rig.edgesFrom(rig.osc), monoPair());

    rig.editor.getPolyChain().polyPillClicked(rig.osc, true);
    EXPECT_EQ(rig.edgesFrom(rig.osc), eightVoicesOntoLeft()) << "Poly on: the pair becomes one sum fan on Left";
    EXPECT_EQ(rig.cablesInto(rig.out), 1);

    ASSERT_TRUE(rig.undo.undo());
    EXPECT_EQ(rig.edgesFrom(rig.osc), monoPair()) << "one undo restores the mono pair exactly";
    ASSERT_TRUE(rig.undo.redo());
    ASSERT_EQ(rig.edgesFrom(rig.osc), eightVoicesOntoLeft());

    rig.editor.getPolyChain().polyPillClicked(rig.osc, false);
    EXPECT_EQ(rig.edgesFrom(rig.osc), monoPair()) << "Poly off: what a fresh mono drop wires, no follower edges";
    EXPECT_EQ(rig.cablesInto(rig.out), 2);

    ASSERT_TRUE(rig.undo.undo());
    EXPECT_EQ(rig.edgesFrom(rig.osc), eightVoicesOntoLeft());
}

TEST_F(GraphEditorTest, HealingAPolyChainIntoAudioOutputSumsEveryVoice) {
    OutputRig rig;
    const auto vca = rig.addModule(std::make_unique<VCAModule>(), 700);
    setPolyParam(rig.oscillator(), true);
    setPolyParam(*rig.engine.getGraph().getNodeForId(vca)->getProcessor(), true);
    rig.cable(rig.osc, 0, vca, 0);
    rig.cable(vca, 0, rig.out, 0);
    ASSERT_EQ(rig.edgesFrom(rig.osc).size(), 0u);
    ASSERT_EQ(rig.edgesFrom(vca), (Edges{{0, 0}})) << "a poly VCA's output is already the summed voices";

    rig.editor.requestDeleteModule(vca);

    ASSERT_EQ(rig.engine.getGraph().getNodeForId(vca), nullptr);
    EXPECT_EQ(rig.edgesFrom(rig.osc), eightVoicesOntoLeft()) << "the heal lands all eight voices on Output";
}

TEST_F(GraphEditorTest, TogglingPolyOnADualOscillatorFoldsItsRightCableIntoTheLeftFan) {
    OutputRig rig;
    setDualIOParam(rig.oscillator(), true);
    rig.cable(rig.osc, 0, rig.out, 0);
    rig.cable(rig.osc, 1, rig.out, 1);
    ASSERT_EQ(rig.edgesFrom(rig.osc), monoPair());

    rig.editor.getPolyChain().polyPillClicked(rig.osc, true);
    EXPECT_EQ(rig.edgesFrom(rig.osc), eightVoicesOntoLeft()) << "Left keeps every voice and Right borrows Left";
    ASSERT_TRUE(rig.undo.undo());
    EXPECT_EQ(rig.edgesFrom(rig.osc), monoPair());
}
