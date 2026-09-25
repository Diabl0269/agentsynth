// =================================================================================================
// FRO25 (P9-3d, docs/mixer/mixer.md#make-channel-and-shared-modules): "Make channel" on a track (header menu) or a
// selected chain (canvas / module menu). The track's exclusive chain moves into a channel macro with the default EQ ->
// Compressor -> Channel Strip -> Master chain; a module another track also uses stays outside (a shared LFO reaches in
// through an auto-created port); a merge point becomes its own bus channel; "Duplicate into Channel" gives this channel
// an independent copy of a shared module; a poly chain gets a Voice Mixer; an already-channeled target is a no-op;
// every action is ONE undo step.
//
// The render-identity tests build the same legacy patch in two Hosted engines, convert one through a
// standalone GraphEditor, and compare the offline renders sample for sample.
// =================================================================================================

#include "../../StubPluginInstance.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Branding.h"
#include "ChannelFlowTestFixture.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/VCAModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginScanService.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <thread>

// -------------------------------------------------------------------------------------------
// Core behaviour, through a standalone GraphEditor (it owns the macros and the port splicing).
// -------------------------------------------------------------------------------------------

TEST(ChannelFlowMakeChannelCore, ExclusiveChainAndItsOwnLfoMoveIntoTheChannelMacro) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    auto& graph = patch.engine.getGraph();
    const auto rig = buildLegacyRigCFT(patch.engine, patch.output, RigShapeCFT::SharedLfo);
    ASSERT_GE(rig.cutoffChannel, 0);

    ASSERT_TRUE(editor.nodeNeedsChannel(rig.trackInA->nodeID));
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));

    const auto* macro = editor.getMacros().findByMember(nodeUuid(rig.trackInA));
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->name, "Lead");
    EXPECT_TRUE(macro->collapsed);
    for (auto* member : {rig.trackInA, rig.oscA, rig.filterA})
        EXPECT_TRUE(macro->hasMember(nodeUuid(member))) << "modules used only by this track move in";
    EXPECT_TRUE(macro->hasMember(nodeUuid(rig.ownLfo))) << "an LFO modulating only this chain moves in too";
    for (auto* outside : {rig.sharedLfo, rig.trackInB, rig.oscB, rig.filterB})
        EXPECT_FALSE(macro->hasMember(nodeUuid(outside))) << "another track's modules never move";

    auto* gate = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::Gate);
    auto* eq = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::ParametricEQ);
    auto* compressor = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::Compressor);
    auto* strip = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::ChannelStrip);
    ASSERT_NE(gate, nullptr);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(compressor, nullptr);
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(dynamic_cast<ModuleBase*>(gate->getProcessor())->isBypassed());
    EXPECT_TRUE(dynamic_cast<ModuleBase*>(eq->getProcessor())->isBypassed());
    EXPECT_TRUE(dynamic_cast<ModuleBase*>(compressor->getProcessor())->isBypassed());
    EXPECT_TRUE(graph.isConnected({{rig.filterA->nodeID, 0}, {gate->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected(
        {{rig.filterA->nodeID, dynamic_cast<ModuleBase*>(rig.filterA->getProcessor())->rightAudioLegChannel()},
         {gate->nodeID, 1}}))
        << "the right leg is read off rightAudioLegChannel(), never assumed to be ch1";
    EXPECT_TRUE(graph.isConnected({{gate->nodeID, 0}, {eq->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{gate->nodeID, 1}, {eq->nodeID, 1}}));

    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(master, nullptr);
    EXPECT_FALSE(macro->hasMember(nodeUuid(master))) << "Master stays outside the macro";
    EXPECT_TRUE(graph.isConnected({{strip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "Strip -> Master stays a PLAIN edge, never a macro port";
    EXPECT_TRUE(graph.isConnected(
        {{strip->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}}));
    EXPECT_FALSE(graph.isConnected({{rig.filterA->nodeID, 0}, {rig.output->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{rig.filterB->nodeID, 0}, {master->nodeID, MasterModule::kDirectLeft}}))
        << "track B keeps its own direct path, now through Master's Direct input";

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_FALSE(editor.nodeNeedsChannel(rig.trackInA->nodeID));
    EXPECT_TRUE(editor.nodeNeedsChannel(rig.trackInB->nodeID)) << "track B is untouched and still channel-less";
}

TEST(ChannelFlowMakeChannelCore, SharedLfoStaysOutsideThroughAnAutoPortAndTheRenderIsIdentical) {
    HostedPatchCFT reference, converted;
    buildLegacyRigCFT(reference.engine, reference.output, RigShapeCFT::SharedLfo);
    const auto rig = buildLegacyRigCFT(converted.engine, converted.output, RigShapeCFT::SharedLfo);
    auto& graph = converted.engine.getGraph();

    GraphEditor editor(converted.engine);
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));
    const auto* macro = editor.getMacros().findByMember(nodeUuid(rig.trackInA));
    ASSERT_NE(macro, nullptr);
    EXPECT_FALSE(macro->hasMember(nodeUuid(rig.sharedLfo)));

    // The shared LFO's leg into Filter A now enters through one of the channel's own inlet ports.
    bool viaOwnInlet = false;
    for (const auto& c : graph.getConnections()) {
        if (c.destination.nodeID != rig.filterA->nodeID || c.destination.channelIndex != rig.cutoffChannel)
            continue;
        auto* source = graph.getNodeForId(c.source.nodeID);
        viaOwnInlet =
            viaOwnInlet || (isModuleOfTypeCFT(source, ModuleType::MacroInlet) && macro->memberIsPort(nodeUuid(source)));
    }
    EXPECT_TRUE(viaOwnInlet) << "a shared module reaches in via an auto-created macro port";
    EXPECT_TRUE(modulatesCFT(graph, rig.sharedLfo, rig.filterA, rig.cutoffChannel));
    EXPECT_TRUE(modulatesCFT(graph, rig.sharedLfo, rig.filterB, rig.cutoffChannel))
        << "the other track keeps the original LFO";

    expectIdenticalRendersCFT(reference.render(16), converted.render(16));
}

TEST(ChannelFlowMakeChannelCore, MergePointBecomesItsOwnBusChannelAndTheRenderIsIdentical) {
    HostedPatchCFT reference, converted;
    buildLegacyRigCFT(reference.engine, reference.output, RigShapeCFT::Merge);
    const auto rig = buildLegacyRigCFT(converted.engine, converted.output, RigShapeCFT::Merge);
    auto& graph = converted.engine.getGraph();

    GraphEditor editor(converted.engine);
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2) << "the track's own strip plus the bus strip";

    const auto* lead = editor.getMacros().findByMember(nodeUuid(rig.trackInA));
    const auto* bus = editor.getMacros().findByMember(nodeUuid(rig.filterB));
    ASSERT_NE(lead, nullptr);
    ASSERT_NE(bus, nullptr);
    ASSERT_NE(lead, bus);
    EXPECT_EQ(bus->name, "Filter Bus");
    EXPECT_TRUE(lead->hasMember(nodeUuid(rig.filterA)));
    EXPECT_FALSE(lead->hasMember(nodeUuid(rig.filterB))) << "the shared effect is never assigned to one track";
    EXPECT_FALSE(bus->hasMember(nodeUuid(rig.oscB))) << "track B's own modules stay outside the bus";

    auto* leadStrip = findMacroMemberOfTypeCFT(graph, *lead, ModuleType::ChannelStrip);
    auto* busStrip = findMacroMemberOfTypeCFT(graph, *bus, ModuleType::ChannelStrip);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(leadStrip, nullptr);
    ASSERT_NE(busStrip, nullptr);
    ASSERT_NE(master, nullptr);
    EXPECT_TRUE(graph.isConnected({{busStrip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}));
    EXPECT_FALSE(graph.isConnected({{leadStrip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "the track reaches Master only through the bus, never twice";
    EXPECT_TRUE(editor.nodeNeedsChannel(rig.trackInB->nodeID)) << "track B can still get its own channel";

    expectIdenticalRendersCFT(reference.render(16), converted.render(16));

    // ...and giving track B its channel afterwards feeds the SAME bus, still rendering identically.
    const auto planB = synth::planMakeChannel(graph, rig.trackInB->nodeID, editor.getMacros());
    ASSERT_TRUE(planB.refusal.isEmpty()) << planB.refusal;
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInB->nodeID, "Pad"));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 3);
    // Re-fetched: MacroSet::add may reallocate, so `bus` above is not safe to compare against.
    const auto* busAfter = editor.getMacros().findByMember(nodeUuid(rig.filterB));
    const auto* pad = editor.getMacros().findByMember(nodeUuid(rig.trackInB));
    ASSERT_NE(busAfter, nullptr);
    ASSERT_NE(pad, nullptr);
    EXPECT_EQ(busAfter->name, "Filter Bus") << "the second track joins the SAME bus, no second one";
    EXPECT_NE(pad, busAfter);
    EXPECT_FALSE(pad->hasMember(nodeUuid(rig.filterB)));
    expectIdenticalRendersCFT(reference.render(16), converted.render(16));
}

TEST(ChannelFlowMakeChannelCore, AlreadyChanneledOrGroupedTargetIsANoOp) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    auto& graph = patch.engine.getGraph();
    const auto rig = buildLegacyRigCFT(patch.engine, patch.output, RigShapeCFT::SharedLfo);
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));

    const auto graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const auto macrosBefore = juce::JSON::toString(editor.getMacros().toVar());
    EXPECT_FALSE(editor.nodeNeedsChannel(rig.trackInA->nodeID));
    EXPECT_FALSE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Again"));
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore);
    EXPECT_EQ(juce::JSON::toString(editor.getMacros().toVar()), macrosBefore);

    // A node that would move but is already in a macro refuses the whole action (flat model).
    synth::Macro handMade;
    handMade.name = "Hand";
    handMade.members = {nodeUuid(rig.oscB)};
    editor.getMacros().add(handMade);
    const auto plan = synth::planMakeChannel(graph, rig.trackInB->nodeID, editor.getMacros());
    EXPECT_TRUE(plan.needsChannel);
    EXPECT_TRUE(plan.refusal.isNotEmpty());
    EXPECT_FALSE(editor.makeChannelFromNode(rig.trackInB->nodeID, "Pad"));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
}

// -------------------------------------------------------------------------------------------
// Adversarial probes (review): exactly-one CV path (no double-drive), a three-way merge, and a
// merge point followed by a SECOND shared effect before the output.
// -------------------------------------------------------------------------------------------

// Counts every path `source` modulates `dest`'s `channel` through, including through a macro
// inlet port -- unlike modulatesCFT (which only proves at least one exists), this catches a splice
// that leaves the old direct edge AND adds a new ported one (double-driving the CV, not just
// changing where it enters).
int countModulationPathsCFT(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node* source,
                            juce::AudioProcessorGraph::Node* dest, int channel) {
    const auto connections = graph.getConnections();
    int count = 0;
    for (const auto& in : connections) {
        if (in.source.nodeID != source->nodeID ||
            !isModuleOfTypeCFT(graph.getNodeForId(in.destination.nodeID), ModuleType::Attenuverter))
            continue;
        for (const auto& out : connections) {
            if (out.source.nodeID != in.destination.nodeID)
                continue;
            if (out.destination.nodeID == dest->nodeID && out.destination.channelIndex == channel)
                ++count;
            if (isModuleOfTypeCFT(graph.getNodeForId(out.destination.nodeID), ModuleType::MacroInlet))
                for (const auto& viaPort : connections)
                    if (viaPort.source.nodeID == out.destination.nodeID && viaPort.destination.nodeID == dest->nodeID &&
                        viaPort.destination.channelIndex == channel)
                        ++count;
        }
    }
    return count;
}

TEST(ChannelFlowMakeChannelCore, SharedLfoEntersThroughExactlyOnePathNoDoubleDrive) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    auto& graph = patch.engine.getGraph();
    const auto rig = buildLegacyRigCFT(patch.engine, patch.output, RigShapeCFT::SharedLfo);
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));
    EXPECT_EQ(countModulationPathsCFT(graph, rig.sharedLfo, rig.filterA, rig.cutoffChannel), 1)
        << "the splice must retarget the crossing, never add a second path alongside the original";
    EXPECT_EQ(countModulationPathsCFT(graph, rig.sharedLfo, rig.filterB, rig.cutoffChannel), 1)
        << "the untouched track's own routing must not be duplicated either";
}

namespace {

// Three MIDI tracks whose Oscillators all feed ONE shared Filter (a three-way merge), which then
// goes straight to Audio Output. No LFOs -- isolates the merge-head/bus logic itself.
struct ThreeWayRigCFT {
    juce::AudioProcessorGraph::Node* trackInA = nullptr;
    juce::AudioProcessorGraph::Node* trackInB = nullptr;
    juce::AudioProcessorGraph::Node* trackInC = nullptr;
    juce::AudioProcessorGraph::Node* oscA = nullptr;
    juce::AudioProcessorGraph::Node* oscB = nullptr;
    juce::AudioProcessorGraph::Node* oscC = nullptr;
    juce::AudioProcessorGraph::Node* filter = nullptr;
};

ThreeWayRigCFT buildThreeWayRigCFT(AudioEngine& engine, juce::AudioProcessorGraph::Node* output) {
    auto& graph = engine.getGraph();
    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    ThreeWayRigCFT rig;
    juce::String unused;
    rig.trackInA = addPlainNodeCFT(graph, "Track In", {0, 0}, unused);
    rig.trackInB = addPlainNodeCFT(graph, "Track In", {0, 300}, unused);
    rig.trackInC = addPlainNodeCFT(graph, "Track In", {0, 600}, unused);
    rig.oscA = addPlainNodeCFT(graph, "Oscillator", {200, 0}, unused);
    rig.oscB = addPlainNodeCFT(graph, "Oscillator", {200, 300}, unused);
    rig.oscC = addPlainNodeCFT(graph, "Oscillator", {200, 600}, unused);
    rig.filter = addPlainNodeCFT(graph, "Filter", {500, 300}, unused);

    for (const auto& pair : {std::array<juce::AudioProcessorGraph::Node*, 2>{rig.trackInA, rig.oscA},
                             {rig.trackInB, rig.oscB},
                             {rig.trackInC, rig.oscC}})
        graph.addConnection({{pair[0]->nodeID, midi}, {pair[1]->nodeID, midi}});

    const int oscRight = dynamic_cast<ModuleBase*>(rig.oscA->getProcessor())->rightAudioLegChannel();
    const int filterRight = dynamic_cast<ModuleBase*>(rig.filter->getProcessor())->rightAudioLegChannel();
    for (const auto [oscLeg, filterLeg, outLeg] : {std::array<int, 3>{0, 0, 0}, {oscRight, filterRight, 1}}) {
        for (auto* osc : {rig.oscA, rig.oscB, rig.oscC})
            graph.addConnection({{osc->nodeID, oscLeg}, {rig.filter->nodeID, filterLeg}});
        graph.addConnection({{rig.filter->nodeID, filterLeg}, {output->nodeID, outLeg}});
    }
    return rig;
}

} // namespace

TEST(ChannelFlowMakeChannelCore, ThreeWayMergeBecomesOneBusChannelAndTheRenderIsIdentical) {
    HostedPatchCFT reference, converted;
    buildThreeWayRigCFT(reference.engine, reference.output);
    const auto rig = buildThreeWayRigCFT(converted.engine, converted.output);
    auto& graph = converted.engine.getGraph();

    GraphEditor editor(converted.engine);
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "A"));
    // Each track has its own exclusive Oscillator feeding the shared Filter, so A gets its own
    // strip (for oscA -> filter) PLUS the shared filter's bus strip -- same shape as the two-track
    // merge test, one track further.
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2) << "A's own strip plus the bus strip";
    auto* busAfterA = editor.getMacros().findByMember(nodeUuid(rig.filter));
    ASSERT_NE(busAfterA, nullptr);
    EXPECT_FALSE(busAfterA->hasMember(nodeUuid(rig.oscB))) << "track B's own module stays outside the bus";
    EXPECT_FALSE(busAfterA->hasMember(nodeUuid(rig.oscC))) << "track C's own module stays outside the bus";
    expectIdenticalRendersCFT(reference.render(16), converted.render(16));

    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInB->nodeID, "B"));
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInC->nodeID, "C"));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 4)
        << "three own strips (A, B, C) plus exactly ONE shared bus strip -- never a second bus for "
           "the same merge head, and the bus is never duplicated across a three-way merge";
    const auto* busAfterAll = editor.getMacros().findByMember(nodeUuid(rig.filter));
    ASSERT_NE(busAfterAll, nullptr);
    EXPECT_EQ(busAfterAll->name, "Filter Bus");
    auto* aMacro = editor.getMacros().findByMember(nodeUuid(rig.oscA));
    auto* bMacro = editor.getMacros().findByMember(nodeUuid(rig.oscB));
    auto* cMacro = editor.getMacros().findByMember(nodeUuid(rig.oscC));
    ASSERT_NE(aMacro, nullptr);
    ASSERT_NE(bMacro, nullptr);
    ASSERT_NE(cMacro, nullptr);
    EXPECT_NE(aMacro, busAfterAll);
    EXPECT_NE(bMacro, busAfterAll);
    EXPECT_NE(cMacro, busAfterAll);
    EXPECT_NE(aMacro, bMacro);
    EXPECT_NE(bMacro, cMacro);
    expectIdenticalRendersCFT(reference.render(16), converted.render(16));
}

// Two MIDI tracks sharing ONE Oscillator directly (no per-track audio node at all, the T173e
// "two tracks, one shared instrument" shape) -- Make Channel on either track must build only the
// shared instrument's bus, no separate (redundant) strip for the track itself.
TEST(ChannelFlowMakeChannelCore, TwoMidiTracksSharingOneInstrumentGetJustTheSharedBus) {
    HostedPatchCFT reference, converted;
    auto buildShared = [](AudioEngine& engine, juce::AudioProcessorGraph::Node* output, juce::String& aUuid,
                          juce::String& bUuid) {
        auto& graph = engine.getGraph();
        constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
        auto* trackInA = addPlainNodeCFT(graph, "Track In", {0, 0}, aUuid);
        auto* trackInB = addPlainNodeCFT(graph, "Track In", {0, 300}, bUuid);
        juce::String unused;
        auto* osc = addPlainNodeCFT(graph, "Oscillator", {200, 150}, unused);
        graph.addConnection({{trackInA->nodeID, midi}, {osc->nodeID, midi}});
        graph.addConnection({{trackInB->nodeID, midi}, {osc->nodeID, midi}});
        const int oscRight = dynamic_cast<ModuleBase*>(osc->getProcessor())->rightAudioLegChannel();
        graph.addConnection({{osc->nodeID, 0}, {output->nodeID, 0}});
        graph.addConnection({{osc->nodeID, oscRight}, {output->nodeID, 1}});
        return osc;
    };
    juce::String refA, refB, aUuid, bUuid;
    buildShared(reference.engine, reference.output, refA, refB);
    auto* osc = buildShared(converted.engine, converted.output, aUuid, bUuid);
    auto& graph = converted.engine.getGraph();

    GraphEditor editor(converted.engine);
    auto* trackInANode = nodeForUuidCFT(graph, aUuid);
    auto* trackInBNode = nodeForUuidCFT(graph, bUuid);
    ASSERT_NE(trackInANode, nullptr);
    ASSERT_NE(trackInBNode, nullptr);
    ASSERT_TRUE(editor.nodeNeedsChannel(trackInANode->nodeID));
    ASSERT_TRUE(editor.makeChannelFromNode(trackInANode->nodeID, "A"));

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1)
        << "the shared instrument gets exactly one channel -- no separate strip for a MIDI track "
           "that contributes no exclusive audio of its own";
    const auto* macro = editor.getMacros().findByMember(nodeUuid(osc));
    ASSERT_NE(macro, nullptr) << "the shared oscillator itself must end up in a channel";
    EXPECT_FALSE(macro->hasMember(aUuid)) << "the Track In itself never moves -- it isn't the channel's audio source";
    // Track B's own header entry must now report "no-op" -- it already shares this channel.
    EXPECT_FALSE(editor.nodeNeedsChannel(trackInBNode->nodeID))
        << "track B already shares this channel and must not offer a second Make Channel";

    expectIdenticalRendersCFT(reference.render(16), converted.render(16));
}

namespace {

// A merge into Filter M1, which then feeds a SECOND shared effect Filter M2 before Audio Output --
// the bus must absorb BOTH shared nodes, not just the merge head.
struct ChainedMergeRigCFT {
    juce::AudioProcessorGraph::Node* trackInA = nullptr;
    juce::AudioProcessorGraph::Node* trackInB = nullptr;
    juce::AudioProcessorGraph::Node* oscA = nullptr;
    juce::AudioProcessorGraph::Node* oscB = nullptr;
    juce::AudioProcessorGraph::Node* filterA = nullptr;
    juce::AudioProcessorGraph::Node* m1 = nullptr;
    juce::AudioProcessorGraph::Node* m2 = nullptr;
};

ChainedMergeRigCFT buildChainedMergeRigCFT(AudioEngine& engine, juce::AudioProcessorGraph::Node* output) {
    auto& graph = engine.getGraph();
    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    ChainedMergeRigCFT rig;
    juce::String unused;
    rig.trackInA = addPlainNodeCFT(graph, "Track In", {0, 0}, unused);
    rig.trackInB = addPlainNodeCFT(graph, "Track In", {0, 400}, unused);
    rig.oscA = addPlainNodeCFT(graph, "Oscillator", {200, 0}, unused);
    rig.oscB = addPlainNodeCFT(graph, "Oscillator", {200, 400}, unused);
    rig.filterA = addPlainNodeCFT(graph, "Filter", {400, 0}, unused);
    rig.m1 = addPlainNodeCFT(graph, "Filter", {600, 200}, unused);
    rig.m2 = addPlainNodeCFT(graph, "Filter", {800, 200}, unused);

    graph.addConnection({{rig.trackInA->nodeID, midi}, {rig.oscA->nodeID, midi}});
    graph.addConnection({{rig.trackInB->nodeID, midi}, {rig.oscB->nodeID, midi}});

    const int oscRight = dynamic_cast<ModuleBase*>(rig.oscA->getProcessor())->rightAudioLegChannel();
    const int filterRight = dynamic_cast<ModuleBase*>(rig.filterA->getProcessor())->rightAudioLegChannel();
    for (const auto [oscLeg, filterLeg, outLeg] : {std::array<int, 3>{0, 0, 0}, {oscRight, filterRight, 1}}) {
        graph.addConnection({{rig.oscA->nodeID, oscLeg}, {rig.filterA->nodeID, filterLeg}});
        graph.addConnection({{rig.filterA->nodeID, filterLeg}, {rig.m1->nodeID, filterLeg}});
        graph.addConnection({{rig.oscB->nodeID, oscLeg}, {rig.m1->nodeID, filterLeg}});
        graph.addConnection({{rig.m1->nodeID, filterLeg}, {rig.m2->nodeID, filterLeg}});
        graph.addConnection({{rig.m2->nodeID, filterLeg}, {output->nodeID, outLeg}});
    }
    return rig;
}

} // namespace

TEST(ChannelFlowMakeChannelCore, MergeFollowedByASecondSharedEffectBoxesBothIntoOneBus) {
    HostedPatchCFT reference, converted;
    buildChainedMergeRigCFT(reference.engine, reference.output);
    const auto rig = buildChainedMergeRigCFT(converted.engine, converted.output);
    auto& graph = converted.engine.getGraph();

    GraphEditor editor(converted.engine);
    ASSERT_TRUE(editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"));
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 2) << "the track's own strip plus the bus strip";

    const auto* bus = editor.getMacros().findByMember(nodeUuid(rig.m1));
    ASSERT_NE(bus, nullptr) << "the merge head (m1) must be boxed into a bus";
    EXPECT_TRUE(bus->hasMember(nodeUuid(rig.m2)))
        << "the SECOND shared effect downstream of the merge joins the SAME bus, not left dangling or double-strapped";

    auto* busStrip = findMacroMemberOfTypeCFT(graph, *bus, ModuleType::ChannelStrip);
    ASSERT_NE(busStrip, nullptr);
    auto* master = findNodeOfTypeCFT(graph, ModuleType::Master);
    ASSERT_NE(master, nullptr);
    EXPECT_TRUE(graph.isConnected({{busStrip->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "the bus strip sits after BOTH shared effects, not spliced in the middle";
    EXPECT_FALSE(graph.isConnected({{rig.m1->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}}))
        << "m1 must not leak straight to Master now that m2 sits between it and the bus strip";

    expectIdenticalRendersCFT(reference.render(16), converted.render(16));
}
