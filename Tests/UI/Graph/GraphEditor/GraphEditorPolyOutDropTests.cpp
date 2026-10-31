// Dropping Poly MIDI's "Poly Out" cable on a mono Oscillator, through the press-drag-release path
// (beginConnectionDrag / dragConnection / endConnectionDrag): the Pitch jack takes it, and a jack that cannot take it
// (the MIDI input: Poly Out carries CV, not MIDI) says so in the status bar instead of dropping the cable unseen.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/PolyMidiModule.h"

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
