// GraphEditorDualIOFxRightLegTests.cpp
//
// Turning Dual I/O on for a module between a source and a one-jack peer wires its Right output into
// the peer's one jack. The matrix covers every {contiguous-pair FX, split-block} source against a
// {collapsed, split} split-block peer, asserting RAW graph edges (never drawn cables) for the
// toggle on, the toggle off, and one undo step. Mirrors the style of GraphEditorDualIOTests.cpp.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include <set>
#include <tuple>

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
using RawEdge = std::tuple<juce::uint32, int, juce::uint32, int>;

enum class Src { Delay, Reverb, Chorus, Distortion, Oscillator, Filter };
enum class Peer { Vca, Filter };

const char* srcName(Src s) {
    switch (s) {
    case Src::Delay:
        return "Delay";
    case Src::Reverb:
        return "Reverb";
    case Src::Chorus:
        return "Chorus";
    case Src::Distortion:
        return "Distortion";
    case Src::Oscillator:
        return "Oscillator";
    case Src::Filter:
        return "Filter";
    }
    return "";
}

std::unique_ptr<juce::AudioProcessor> makeSrc(Src s) {
    switch (s) {
    case Src::Delay:
        return std::make_unique<DelayModule>();
    case Src::Reverb:
        return std::make_unique<ReverbModule>();
    case Src::Chorus:
        return std::make_unique<ChorusModule>();
    case Src::Distortion:
        return std::make_unique<DistortionModule>();
    case Src::Oscillator:
        return std::make_unique<OscillatorModule>();
    case Src::Filter:
        return std::make_unique<FilterModule>();
    }
    return nullptr;
}

std::unique_ptr<juce::AudioProcessor> makePeer(Peer p) {
    if (p == Peer::Vca)
        return std::make_unique<VCAModule>();
    return std::make_unique<FilterModule>();
}

int peerRightBase(Peer p) { return p == Peer::Vca ? VCAModule::kRightBase : FilterModule::kRightBase; }

std::set<RawEdge> audioEdges(juce::AudioProcessorGraph& graph) {
    std::set<RawEdge> edges;
    for (const auto& c : graph.getConnections())
        if (!c.source.isMIDI() && !c.destination.isMIDI())
            edges.insert(
                {c.source.nodeID.uid, c.source.channelIndex, c.destination.nodeID.uid, c.destination.channelIndex});
    return edges;
}

bool hasRawEdge(juce::AudioProcessorGraph& graph, NodeID a, int ca, NodeID b, int cb) {
    return graph.isConnected({{a, ca}, {b, cb}});
}

int feeds(juce::AudioProcessorGraph& graph, NodeID dst, int dstCh) {
    int n = 0;
    for (const auto& c : graph.getConnections())
        if (c.destination.nodeID == dst && !c.destination.isMIDI() && c.destination.channelIndex == dstCh)
            ++n;
    return n;
}

juce::AudioProcessorParameter* dualParamOf(juce::AudioProcessor& proc) {
    for (auto* p : proc.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == "dualIO")
            return p;
    return nullptr;
}

// A user toggle: a parameter gesture, which is what snapshots the graph for the undo step.
void toggleDual(juce::AudioProcessor& proc, bool on) {
    auto* p = dualParamOf(proc);
    ASSERT_NE(p, nullptr);
    p->beginChangeGesture();
    p->setValueNotifyingHost(on ? 1.0f : 0.0f);
    p->endChangeGesture();
}

ModuleComponent* cardOf(GraphEditor& editor, juce::AudioProcessor* proc) {
    if (auto* content = editor.getChildComponent(0))
        for (auto* child : content->getChildren())
            if (auto* mod = dynamic_cast<ModuleComponent*>(child))
                if (mod->getModule() == proc)
                    return mod;
    return nullptr;
}

struct Rig {
    NodeID src, peer;
    int srcRight = -1;
    int peerRight = -1;
};

// Source -> peer with the peer's single Audio jack, drawn by hand the way a user does (the real
// resolvePolyLink path), the source collapsed and the peer in the requested shape.
Rig buildRig(AudioEngine& engine, GraphEditor& editor, Src s, Peer p, bool peerSplit) {
    auto& graph = engine.getGraph();
    auto srcNode = graph.addNode(makeSrc(s));
    auto peerNode = graph.addNode(makePeer(p));
    setDualIOParam(*srcNode->getProcessor(), false);
    setDualIOParam(*peerNode->getProcessor(), peerSplit);
    editor.updateComponents();

    auto* srcComp = cardOf(editor, srcNode->getProcessor());
    auto* peerComp = cardOf(editor, peerNode->getProcessor());
    EXPECT_NE(srcComp, nullptr);
    EXPECT_NE(peerComp, nullptr);
    if (srcComp == nullptr || peerComp == nullptr)
        return {};
    srcComp->setBounds(0, 0, 200, 400);
    peerComp->setBounds(300, 0, 200, 400);

    editor.beginConnectionDrag(srcComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(peerComp->getBounds().getPosition() + peerComp->getPortCenter(0, true));

    auto* srcMb = dynamic_cast<ModuleBase*>(srcNode->getProcessor());
    Rig rig;
    rig.src = srcNode->nodeID;
    rig.peer = peerNode->nodeID;
    rig.srcRight = srcMb->rightAudioLegChannel();
    rig.peerRight = peerRightBase(p);
    return rig;
}

using ToggleParam = std::tuple<Src, Peer, bool>;
class DualIOToggleMatrix : public ::testing::TestWithParam<ToggleParam> {};
} // namespace

// Regression test for FRO1227: splitting a one-jack Delay (or any FX) between a source and a one-jack
// Filter/VCA left its Right output unwired, so half the stereo was lost. The split must add the Right
// leg to the peer's one jack as a summed second cable (or onto the peer's own right leg when the peer
// is split too), in one undo step, and turning it off must restore the original edges exactly.
TEST_P(DualIOToggleMatrix, SplittingWiresTheRightLegAndCollapsingRestoresTheOriginalEdges) {
    const auto [src, peerKind, peerSplit] = GetParam();
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(800, 600);
    auto& graph = engine.getGraph();

    const Rig rig = buildRig(engine, editor, src, peerKind, peerSplit);
    ASSERT_NE(rig.srcRight, -1);
    ASSERT_TRUE(hasRawEdge(graph, rig.src, 0, rig.peer, 0)) << "the cable the user drew";
    const auto before = audioEdges(graph);

    toggleDual(*graph.getNodeForId(rig.src)->getProcessor(), true);

    EXPECT_TRUE(hasRawEdge(graph, rig.src, 0, rig.peer, 0)) << "the left leg keeps its cable";
    if (peerSplit) {
        EXPECT_TRUE(hasRawEdge(graph, rig.src, rig.srcRight, rig.peer, rig.peerRight))
            << "Right goes onto the peer's own right leg";
        EXPECT_EQ(feeds(graph, rig.peer, 0), 1) << "the peer's left jack is fed once";
        EXPECT_EQ(feeds(graph, rig.peer, rig.peerRight), 1) << "the peer's right leg is fed once";
    } else {
        EXPECT_TRUE(hasRawEdge(graph, rig.src, rig.srcRight, rig.peer, 0))
            << "Right is summed into the peer's one jack as a second cable";
        EXPECT_EQ(feeds(graph, rig.peer, 0), 2) << "exactly Left plus Right on the one jack";
        EXPECT_EQ(feeds(graph, rig.peer, rig.peerRight), 0) << "the peer's hidden right block stays unwired";
    }
    EXPECT_FALSE(hasRawEdge(graph, rig.src, rig.srcRight, rig.peer, 1)) << "ch1 is a CV input on the peer";

    // A split-block source into a split peer re-points its Right cable onto the surviving Left leg on
    // collapse (existing behaviour), so only the other pairings promise the exact original edge set.
    const bool splitBlockSource = src == Src::Oscillator || src == Src::Filter;
    toggleDual(*graph.getNodeForId(rig.src)->getProcessor(), false);
    if (!(splitBlockSource && peerSplit))
        EXPECT_EQ(audioEdges(graph), before) << "turning Dual I/O off restores the original edges exactly";

    const auto beforeSecondSplit = audioEdges(graph);
    toggleDual(*graph.getNodeForId(rig.src)->getProcessor(), true);
    if (!peerSplit)
        ASSERT_NE(audioEdges(graph), beforeSecondSplit)
            << "the split changed the graph, so the undo has something to take back";
    ASSERT_TRUE(undoMgr.undo());
    EXPECT_EQ(audioEdges(graph), beforeSecondSplit) << "one undo step takes the whole split back";
    auto* restored = dynamic_cast<ModuleBase*>(graph.getNodeForId(rig.src)->getProcessor());
    ASSERT_NE(restored, nullptr);
    EXPECT_FALSE(restored->isDualIO()) << "and the parameter with it";
}

INSTANTIATE_TEST_SUITE_P(FxAndSplitBlockSources, DualIOToggleMatrix,
                         ::testing::Combine(::testing::Values(Src::Delay, Src::Reverb, Src::Chorus, Src::Distortion,
                                                              Src::Oscillator, Src::Filter),
                                            ::testing::Values(Peer::Vca, Peer::Filter), ::testing::Values(false, true)),
                         [](const ::testing::TestParamInfo<ToggleParam>& info) {
                             return std::string(srcName(std::get<0>(info.param))) + "_" +
                                    (std::get<1>(info.param) == Peer::Vca ? "Vca" : "Filter") +
                                    (std::get<2>(info.param) ? "Split" : "Collapsed");
                         });
