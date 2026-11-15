// A cable from a Macros output onto a knob of a module in a track is a modulation cable like any other: on a mono or a
// poly Oscillator, to any of its knobs (Glide included), every leg of it has the mod colour and the target knob shows
// its ring. And the guard under it: a cable from any kind of jack (a Macro In's audio-role jack, a Macros output) onto
// any module's knob jack resolves to that knob's own raw channel. docs/layout/cables.md.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/MacroControlModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include <gtest/gtest.h>

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
