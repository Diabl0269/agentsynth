// GraphEditor poly-link tests: pure resolvePolyLink pairing/scoring (no graph needed), Dual I/O toggle tests for
// keeping/completing both stereo legs, and Macros cables into a track reaching each knob's own channel.
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/ADSRModule.h"
#include "Modules/FX/CompressorModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MacroControlModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include <gtest/gtest.h>

// ============================================================================
// Poly connections auto-fan-out
// ============================================================================

// ---- Pure resolvePolyLink tests (no graph needed) ----

TEST_F(GraphEditorTest, ResolvePolyLinkFansEnvelopeToPolyVCA) {
    ADSRModule adsr;
    VCAModule vca;
    setPolyParam(adsr, true);
    setPolyParam(vca, true);

    auto link = GraphEditor::resolvePolyLink(&adsr, 0, &vca, 2); // VCA CV is jack 2, behind Audio L/R
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 8);
    EXPECT_EQ(link.voiceCount, 8);
}

TEST_F(GraphEditorTest, ResolvePolyLinkPicksFanMatchingRole) {
    PolyMidiModule polyMidi;
    OscillatorModule osc;
    ADSRModule adsr;
    setPolyParam(osc, true);
    setPolyParam(adsr, true);

    // PolyMidi's "Poly Out" jack fronts both a Pitch fan (raw 0) and a Gate fan (raw 8).
    // Into the Oscillator's Pitch input, the Pitch fan must win.
    auto linkToOsc = GraphEditor::resolvePolyLink(&polyMidi, 0, &osc, 0);
    EXPECT_EQ(linkToOsc.sourceRawChannel, 0);
    EXPECT_EQ(linkToOsc.destRawChannel, 0);
    EXPECT_EQ(linkToOsc.voiceCount, 8);

    // Into the ADSR's Gate input (same source visible jack), the Gate fan must win instead.
    auto linkToAdsr = GraphEditor::resolvePolyLink(&polyMidi, 0, &adsr, 0);
    EXPECT_EQ(linkToAdsr.sourceRawChannel, 8);
    EXPECT_EQ(linkToAdsr.destRawChannel, 0);
    EXPECT_EQ(linkToAdsr.voiceCount, 8);
}

TEST_F(GraphEditorTest, ResolvePolyLinkStaysMonoWhenDestIsMono) {
    ADSRModule polyAdsr;
    setPolyParam(polyAdsr, true);
    VCAModule monoVca; // poly defaults to false

    // A poly source into a mono jack must not sum eight envelopes onto one CV channel.
    auto link = GraphEditor::resolvePolyLink(&polyAdsr, 0, &monoVca, 2); // CV jack
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 1);
    EXPECT_EQ(link.voiceCount, 1);
    EXPECT_EQ(link.sourceStride, 1);
}

TEST_F(GraphEditorTest, ResolvePolyLinkBroadcastsMonoSourceAcrossModCvFan) {
    // One mono modulator on a per-voice mod-CV fan drives every voice: all eight wires leave the
    // same source channel (sourceStride == 0) and land on VCA raw channels 8-15.
    LFOModule lfo;
    VCAModule polyVca;
    setPolyParam(polyVca, true);

    auto link = GraphEditor::resolvePolyLink(&lfo, 0, &polyVca, 2); // CV jack
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 8);
    EXPECT_EQ(link.voiceCount, 8);
    EXPECT_EQ(link.sourceStride, 0);
}

TEST_F(GraphEditorTest, ResolvePolyLinkBroadcastsMonoIntoCollapsedStereoPair) {
    // Collapsed Dual I/O (voiceSpan 2, PortRole::Audio) is a stereo bus, not a poly voice fan —
    // mono sources duplicate onto L and R so a single cable feeds both FX legs.
    OscillatorModule osc;
    DelayModule delay; // Dual I/O defaults off → one Audio jack spanning raw 0/1

    auto link = GraphEditor::resolvePolyLink(&osc, 0, &delay, 0);
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 0);
    EXPECT_EQ(link.voiceCount, 2);
    EXPECT_EQ(link.sourceStride, 0);
}

TEST_F(GraphEditorTest, ResolvePolyLinkTreatsACollapsedKeyJackAsAStereoPair) {
    // The collapsed "Key" jack (PortRole::Sidechain, span 2 on raw 7/8) takes the same
    // cables the collapsed Audio jack does — a mono kick keys both legs, a stereo source pairs L/R.
    OscillatorModule osc;
    DelayModule delay; // collapsed: one Audio output jack spanning raw 0/1
    CompressorModule comp;
    const int keyJack = 6;

    auto mono = GraphEditor::resolvePolyLink(&osc, 0, &comp, keyJack);
    EXPECT_EQ(mono.destRawChannel, CompressorModule::kKeyBase);
    EXPECT_EQ(mono.voiceCount, 2);
    EXPECT_EQ(mono.sourceStride, 0);

    auto stereo = GraphEditor::resolvePolyLink(&delay, 0, &comp, keyJack);
    EXPECT_EQ(stereo.sourceRawChannel, 0);
    EXPECT_EQ(stereo.destRawChannel, CompressorModule::kKeyBase);
    EXPECT_EQ(stereo.voiceCount, 2);
    EXPECT_EQ(stereo.sourceStride, 1);
}

TEST_F(GraphEditorTest, TogglingDualIOKeepsBothStereoLegs) {
    // Dual I/O only changes jack visibility. A collapsed Audio cable fans onto raw ch0 and ch1;
    // flipping Dual I/O on must not drop the right leg.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto delayNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    editor.updateComponents();

    ModuleComponent* oscComp = nullptr;
    ModuleComponent* delayComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
                if (mod->getModule() == delayNode->getProcessor())
                    delayComp = mod;
            }
        }
    }
    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(delayComp, nullptr);

    oscComp->setBounds(0, 0, 100, 100);
    delayComp->setBounds(200, 0, 100, 100);

    editor.beginConnectionDrag(oscComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(delayComp->getBounds().getPosition() + delayComp->getPortCenter(0, true));

    auto& graph = engine.getGraph();
    auto hasEdge = [&](int srcCh, int dstCh) {
        for (auto& conn : graph.getConnections())
            if (conn.source.nodeID == oscNode->nodeID && conn.source.channelIndex == srcCh &&
                conn.destination.nodeID == delayNode->nodeID && conn.destination.channelIndex == dstCh)
                return true;
        return false;
    };
    ASSERT_TRUE(hasEdge(0, 0));
    ASSERT_TRUE(hasEdge(0, 1)) << "collapsed Audio jack must fan onto Delay Right before the toggle";

    setDualIOParam(*delayNode->getProcessor(), true);

    EXPECT_TRUE(hasEdge(0, 0)) << "Left leg must survive Dual I/O on";
    // The right leg is still fed — but by the Oscillator's own Audio R block, not by a second copy
    // of Audio L. The collapsed jack's mono duplicate has to go with it: keeping both would sum
    // L+R into the Delay's Right input (+6 dB on one side) behind what draws as a single cable.
    //
    // RECONCILED with the later ruling that a module split while wired must come up with both legs
    // live (SplittingAMidChainVoiceModule*): that ruling introduced a mono BROADCAST, and this test
    // is not in tension with it. A broadcast is the fallback for a peer that has no right leg at
    // all; here the Oscillator upstream is dual and owns a real Audio R, so the real leg wins and
    // the stand-in copy is removed. The two rules are one rule: prefer the peer's right leg, copy
    // the left only when there is none. Pinned from the other direction by
    // SplittingAMidChainVoiceModulePrefersRealRightLegsOverABroadcast.
    EXPECT_TRUE(hasEdge(OscillatorModule::kRightBase, 1)) << "Right leg must survive Dual I/O on";
    EXPECT_FALSE(hasEdge(0, 1)) << "the collapsed jack's duplicate of Audio L must not double-feed Right";

    setDualIOParam(*delayNode->getProcessor(), false);

    EXPECT_TRUE(hasEdge(0, 0));
    EXPECT_TRUE(hasEdge(OscillatorModule::kRightBase, 1)) << "Right leg must survive Dual I/O off as well";
}

TEST_F(GraphEditorTest, TogglingDualIOCompletesStereoOutputPair) {
    // A collapsed Audio output dropped on a 2-channel dest (Audio Output, or Dual I/O) often
    // only records the left edge. Toggling Dual I/O on must add the matching right edge.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto srcNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    auto dstNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    editor.updateComponents();

    ModuleComponent* srcComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild))
                if (mod->getModule() == srcNode->getProcessor())
                    srcComp = mod;
        }
    }
    ASSERT_NE(srcComp, nullptr);

    auto& graph = engine.getGraph();
    ASSERT_TRUE(graph.addConnection({{srcNode->nodeID, 0}, {dstNode->nodeID, 0}}));
    {
        bool hasRightBefore = false;
        for (const auto& conn : graph.getConnections())
            if (conn.source.nodeID == srcNode->nodeID && conn.source.channelIndex == 1)
                hasRightBefore = true;
        ASSERT_FALSE(hasRightBefore);
    }

    setDualIOParam(*srcNode->getProcessor(), true);

    bool hasRight = false;
    for (const auto& conn : graph.getConnections())
        if (conn.source.nodeID == srcNode->nodeID && conn.source.channelIndex == 1 &&
            conn.destination.nodeID == dstNode->nodeID && conn.destination.channelIndex == 1)
            hasRight = true;
    EXPECT_TRUE(hasRight) << "Dual I/O on must pair Delay Right out to the dest Right in";
}

TEST_F(GraphEditorTest, DualIOOnSourceDrawsRightCableOntoCollapsedDest) {
    // Both Dual I/O off: one Audio→Audio cable, two raw edges. Splitting only the source must
    // still draw the Right output onto the dest's remaining Audio jack — not wait until the dest
    // is split too (raw ch1 used to be treated as hidden because dest visibleCount == 1).
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto srcNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    auto dstNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    editor.updateComponents();

    ModuleComponent* srcComp = nullptr;
    ModuleComponent* dstComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == srcNode->getProcessor())
                    srcComp = mod;
                if (mod->getModule() == dstNode->getProcessor())
                    dstComp = mod;
            }
        }
    }
    ASSERT_NE(srcComp, nullptr);
    ASSERT_NE(dstComp, nullptr);
    srcComp->setBounds(0, 0, 200, 200);
    dstComp->setBounds(300, 0, 200, 200);

    editor.beginConnectionDrag(srcComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(dstComp->getBounds().getPosition() + dstComp->getPortCenter(0, true));

    setDualIOParam(*srcNode->getProcessor(), true);
    ASSERT_FALSE(dynamic_cast<ModuleBase*>(dstNode->getProcessor())->isDualIO());

    bool drawnFromRight = false;
    for (const auto& cable : editor.buildVisibleCables())
        if (cable.id.srcUid == srcNode->nodeID.uid && cable.id.dstUid == dstNode->nodeID.uid && cable.id.srcPort == 1)
            drawnFromRight = true;
    EXPECT_TRUE(drawnFromRight) << "Right out must draw onto the dest Audio jack while dest Dual I/O is still off";
}

TEST_F(GraphEditorTest, DualIOOnDestDrawsRightCableFromCollapsedSource) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto srcNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    auto dstNode = engine.getGraph().addNode(std::make_unique<DelayModule>());
    editor.updateComponents();

    ModuleComponent* srcComp = nullptr;
    ModuleComponent* dstComp = nullptr;
    auto* content = editor.getChildComponent(0);
    if (content) {
        for (auto* contentChild : content->getChildren()) {
            if (auto* mod = dynamic_cast<ModuleComponent*>(contentChild)) {
                if (mod->getModule() == srcNode->getProcessor())
                    srcComp = mod;
                if (mod->getModule() == dstNode->getProcessor())
                    dstComp = mod;
            }
        }
    }
    ASSERT_NE(srcComp, nullptr);
    ASSERT_NE(dstComp, nullptr);
    srcComp->setBounds(0, 0, 200, 200);
    dstComp->setBounds(300, 0, 200, 200);

    editor.beginConnectionDrag(srcComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(dstComp->getBounds().getPosition() + dstComp->getPortCenter(0, true));

    setDualIOParam(*dstNode->getProcessor(), true);
    ASSERT_FALSE(dynamic_cast<ModuleBase*>(srcNode->getProcessor())->isDualIO());

    bool drawnOntoRight = false;
    for (const auto& cable : editor.buildVisibleCables())
        if (cable.id.srcUid == srcNode->nodeID.uid && cable.id.dstUid == dstNode->nodeID.uid && cable.id.dstPort == 1)
            drawnOntoRight = true;
    EXPECT_TRUE(drawnOntoRight)
        << "Dest Right in must draw from the source Audio jack while source Dual I/O is still off";
}

// Dropping Poly MIDI's "Poly Out" cable on a mono Oscillator, through the press-drag-release path
// (beginConnectionDrag / dragConnection / endConnectionDrag): the Pitch jack takes it, and a jack that cannot take it
// (the MIDI input: Poly Out carries CV, not MIDI) says so in the status bar instead of dropping the cable unseen.
namespace {

struct PolyOutDropRig {
    AudioEngine engine;
    GraphEditor editor{engine};
    juce::AudioProcessorGraph::Node::Ptr polyMidiNode;
    juce::AudioProcessorGraph::Node::Ptr oscNode;
    juce::AudioProcessorGraph::Node::Ptr filterNode;
    ModuleComponent* polyMidiComp = nullptr;
    ModuleComponent* oscComp = nullptr;
    juce::String lastStatus;

    // A mono Oscillator that already has an audio cable (to a Filter), so it does not join Poly MIDI's poly state.
    PolyOutDropRig() {
        editor.setSize(800, 600);
        polyMidiNode = engine.getGraph().addNode(std::make_unique<PolyMidiModule>());
        oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
        filterNode = engine.getGraph().addNode(std::make_unique<FilterModule>());
        setPolyParam(*oscNode->getProcessor(), false);
        setPolyParam(*filterNode->getProcessor(), false);
        editor.updateComponents();

        ModuleComponent* filterComp = nullptr;
        for (auto* child : editor.getChildComponent(0)->getChildren())
            if (auto* mod = dynamic_cast<ModuleComponent*>(child)) {
                if (mod->getModule() == polyMidiNode->getProcessor())
                    polyMidiComp = mod;
                if (mod->getModule() == oscNode->getProcessor())
                    oscComp = mod;
                if (mod->getModule() == filterNode->getProcessor())
                    filterComp = mod;
            }
        polyMidiComp->setBounds(0, 0, 100, 100);
        oscComp->setBounds(200, 0, 100, 100);
        filterComp->setBounds(400, 0, 100, 100);
        drag(*oscComp, 0, false, false, *filterComp, filterComp->getPortCenter(0, true));
        editor.onStatusMessage = [this](const juce::String& msg) { lastStatus = msg; };
    }

    void drag(ModuleComponent& from, int jack, bool fromInput, bool isMidi, ModuleComponent& to,
              juce::Point<int> toLocal) {
        editor.beginConnectionDrag(&from, jack, fromInput, isMidi, juce::Point<int>(0, 0));
        editor.dragConnection(juce::Point<int>(50, 0));
        editor.endConnectionDrag(to.getBounds().getPosition() + toLocal);
    }

    bool polyMidiCabledTo(juce::AudioProcessorGraph::NodeID dst) {
        for (const auto& c : engine.getGraph().getConnections())
            if (c.source.nodeID == polyMidiNode->nodeID && c.destination.nodeID == dst)
                return true;
        return false;
    }
};

} // namespace

// Poly Out dragged onto an already-cabled mono Oscillator's Pitch jack makes a cable (the chain By pitch relies on).
TEST_F(GraphEditorTest, PolyOutDropsOnACabledMonoOscillatorsPitchJack) {
    PolyOutDropRig rig;
    ASSERT_FALSE(rig.polyMidiCabledTo(rig.oscNode->nodeID));

    rig.drag(*rig.polyMidiComp, 0, false, false, *rig.oscComp, rig.oscComp->getPortCenter(0, true));

    EXPECT_TRUE(rig.polyMidiCabledTo(rig.oscNode->nodeID));
}

// Regression test for FRO1293: a drop on a jack of the wrong kind made no cable and said nothing.
TEST_F(GraphEditorTest, PolyOutDroppedOnTheMidiInputIsRefusedWithAMessage) {
    PolyOutDropRig rig;

    rig.drag(*rig.polyMidiComp, 0, false, false, *rig.oscComp, rig.oscComp->getMidiPortCenter(false));

    EXPECT_FALSE(rig.polyMidiCabledTo(rig.oscNode->nodeID));
    EXPECT_TRUE(rig.lastStatus.containsIgnoreCase("midi")) << rig.lastStatus;
}

// ============================================================================
// A cable from a Macros output onto a knob of a module in a track is a modulation cable like any other: on a mono or a
// poly Oscillator, to any of its knobs (Glide included), every leg has the mod colour and the knob shows its ring. And
// the guard under it: a cable from any kind of jack (a Macro In's audio-role jack, a Macros output) onto any module's
// knob jack resolves to that knob's own raw channel. docs/layout/cables.md.
// ============================================================================

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

// An Oscillator grouped with an envelope into a track-like macro, and a Macros module outside it.
struct TrackRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID macros, osc, env;

    explicit TrackRig(bool poly) {
        engine.getGraph().setPlayConfigDetails(2, 2, 44100.0, 512);
        undo.setGraphEditor(&editor);
        editor.setSize(3000, 1600);
        osc = add("Oscillator", 900);
        env = add("ADSR", 1300);
        macros = add("Macros", 100);
        for (const auto id : {osc, env})
            synth::setProcessorPoly(engine.getGraph().getNodeForId(id)->getProcessor(), poly);
        editor.setSelectedNodes({osc, env}); // a macro groups two or more modules
        groupId = editor.getMacroController().groupSelectionIntoMacro(true);
        editor.updateComponents();
    }

    juce::String groupId;

    NodeID add(const juce::String& type, int x) {
        auto node = engine.getGraph().addNode(synth::AIStateMapper::createModule(type));
        const auto uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            module->setNodeUuid(uuid);
        node->properties.set("x", x);
        node->properties.set("y", 100);
        editor.updateComponents();
        return node->nodeID;
    }
    ModuleBase* module(NodeID id) {
        return dynamic_cast<ModuleBase*>(engine.getGraph().getNodeForId(id)->getProcessor());
    }

    int channelOf(const juce::String& paramId) {
        for (const auto& t : module(osc)->getModulationTargets())
            if (t.paramId == paramId)
                return t.channelIndex;
        return -1;
    }

    // A cable dragged from Macros output `macro` onto the jack of the Oscillator knob `paramId`, across the track's
    // boundary: the same path a mouse drop takes, which mints the track's input port.
    void cable(int macro, const juce::String& paramId) {
        const int channel = channelOf(paramId);
        ASSERT_GE(channel, 0) << paramId;
        const int jack = module(osc)->mapInputChannel(channel).visibleJackIndex;
        ASSERT_TRUE(editor.getMacroController().maybeAutoCreateMacroPortsForDrag(macros, macro, osc, jack,
                                                                                 /*isMidi*/ false, true));
        editor.timerCallback();
    }
};

} // namespace

class MacrosModCable : public ::testing::TestWithParam<bool> {};

// Regression test for FRO1860: on an Oscillator track fed from a Macros module, two of three cables were white and the
// Glide knob showed no ring: the track's input port reached Glide's jack on an empty channel, and a cable into a port
// was always drawn as audio.
TEST_P(MacrosModCable, EveryCableFromAMacrosModuleIntoATrackIsAModCableWithARingOnItsKnob) {
    TrackRig rig(GetParam());
    ASSERT_FALSE(rig.groupId.isEmpty());
    const juce::StringArray knobs{"octave", "level", "glide"};
    for (int i = 0; i < knobs.size(); ++i)
        rig.cable(i, knobs[i]);

    int cables = 0;
    for (const auto& c : rig.editor.buildVisibleCables()) {
        ++cables;
        EXPECT_EQ(c.signal, synth::ui::CableSignal::ModCV)
            << "cable to node " << c.destNodeId << " channel " << c.destChannel << ": every leg has the mod colour";
    }
    EXPECT_GE(cables, knobs.size()) << "each macro has a cable on screen (the inner legs hide in the collapsed track)";

    for (const auto& paramId : knobs) {
        const int channel = rig.channelOf(paramId);
        int rings = 0;
        for (const auto& info : rig.editor.getCachedModDisplayInfo())
            if (info.destNodeID == rig.osc && info.destChannelIndex == channel)
                ++rings;
        EXPECT_EQ(rings, 1) << paramId << ": the knob shows its ring";
    }
}

INSTANTIATE_TEST_SUITE_P(MonoAndPoly, MacrosModCable, ::testing::Values(false, true),
                         [](const auto& info) { return info.param ? "Poly" : "Mono"; });

// The guard for "what a jack shows and the raw channel it wires disagree", from the jack side: every module type the
// app can create, mono and poly, every modulation target that has a jack of its own. A cable onto that jack from a
// Macro In (an audio-role jack, the port every cable into a track arrives through) or from a Macros output (a mod
// jack) must reach the target's own raw channel, never an empty channel that shares the jack's number.
TEST(MacroPortCable, ACableFromAnyKindOfJackOntoAKnobsJackReachesThatKnobsOwnChannel) {
    MacroInletModule inlet;
    MacroControlModule macros;
    const std::vector<std::pair<const char*, const ModuleBase*>> sources{{"Macro In", &inlet}, {"Macros", &macros}};
    int checked = 0;
    for (const auto& typeName : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto processor = synth::AIStateMapper::createModule(typeName);
        auto* module = dynamic_cast<ModuleBase*>(processor.get());
        if (module == nullptr)
            continue;
        for (const bool poly : {false, true}) {
            synth::setProcessorPoly(processor.get(), poly);
            for (const auto& target : module->getModulationTargets()) {
                const auto port = module->mapInputChannel(target.channelIndex);
                if (!port.isPolyGroupHead || port.role != PortRole::ModCV ||
                    port.visibleJackIndex >= module->getVisibleInputPortCount())
                    continue; // a knob with no jack of its own (cable-only targets are the knob-drop sweep's)
                for (const auto& [sourceName, source] : sources) {
                    SCOPED_TRACE(typeName + (poly ? " (poly) / " : " / ") + target.name + " from " + sourceName);
                    const auto link = GraphEditor::resolvePolyLink(source, 0, module, port.visibleJackIndex);
                    EXPECT_EQ(link.destRawChannel, target.channelIndex) << "the cable reaches the knob's own channel";
                    ++checked;
                }
            }
        }
    }
    EXPECT_GT(checked, 200) << "the sweep reaches the modules' knob jacks";
}

// Regression test for FRO1860: a project saved while the bug stood holds the Glide cable on the mono Oscillator's empty
// raw channel 10; it opens with that cable on Glide's own channel.
TEST(MacroPortCable, AProjectSavedWithACableOnAnEmptyChannelOpensWithItOnTheKnobsChannel) {
    const auto json = juce::JSON::parse(R"({"nodes": [{"id": 1, "type": "LFO"}, {"id": 2, "type": "Oscillator"}],
        "connections": [{"src": 1, "srcPort": 0, "dst": 2, "dstPort": 10, "isMidi": false}]})");
    juce::AudioProcessorGraph graph;
    std::map<int, NodeID> ids;
    ASSERT_TRUE(
        synth::AIStateMapper::applyJSONToGraph(json, graph, true, /*trusted*/ true, /*autoConnect*/ false, &ids));
    std::vector<int> landed;
    for (const auto& c : graph.getConnections())
        if (c.destination.nodeID == ids[2])
            landed.push_back(c.destination.channelIndex);
    EXPECT_EQ(landed, std::vector<int>{OscillatorModule::kGlideCVChannel});
}
