// PortConnectionList.cpp -- which cables land on a jack and what the far end of each is called.
// docs/layout/cables.md#port-connections-panel.

#include "PortConnectionList.h"

#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModDot/KnobModSources.h"

namespace synth::ui {

namespace {

juce::String dotSeparator() { return juce::String::fromUTF8(" \xC2\xB7 "); }

// A cable names its jacks by RAW channel; the card, the labels and a click speak in visible jacks.
int visibleJackOf(juce::AudioProcessor* processor, int rawChannel, bool isInput) {
    if (auto* module = dynamic_cast<ModuleBase*>(processor))
        return isInput ? module->mapInputChannel(rawChannel).visibleJackIndex
                       : module->mapOutputChannel(rawChannel).visibleJackIndex;
    return rawChannel;
}

// The same names the card's screen-reader stand-ins use (ModuleComponent::syncPortAccessibility).
juce::String jackLabel(juce::AudioProcessor* processor, int visibleJack, bool isInput) {
    juce::String label;
    if (auto* module = dynamic_cast<ModuleBase*>(processor))
        label = isInput ? module->getInputPortLabel(visibleJack) : module->getOutputPortLabel(visibleJack);
    else if (visibleJack == 0)
        label = "Left";
    else if (visibleJack == 1)
        label = "Right";
    if (label.isEmpty())
        label = (isInput ? "In " : "Out ") + juce::String(visibleJack);
    return label;
}

juce::String midiLabel(bool isInput) { return isInput ? "Midi input" : "Midi output"; }

juce::AudioProcessor* processorFor(GraphEditor& editor, juce::AudioProcessorGraph::NodeID id) {
    auto* node = editor.getAudioEngine().getGraph().getNodeForId(id);
    return node != nullptr ? node->getProcessor() : nullptr;
}

// The name of a jack by node and raw channel. A macro port has no module worth naming: it reads as its own name.
juce::String endName(GraphEditor& editor, const GraphEditor::VisibleCable& cable, bool farIsDestination) {
    const juce::AudioProcessorGraph::NodeID id{farIsDestination ? cable.id.dstUid : cable.id.srcUid};
    auto* processor = processorFor(editor, id);
    if (processor == nullptr)
        return {};
    if (const auto owner = editor.getMacroController().macroPortOwnerFor(id); owner.port != nullptr)
        return owner.port->name;
    juce::String port;
    if (cable.signal == CableSignal::Midi) {
        port = midiLabel(farIsDestination);
    } else if (farIsDestination && cable.landsOnKnob) {
        // Wired to a knob: the knob is what the user sees, the hidden CV jack has no honest name.
        const auto target = knobModTarget(editor, id, cable.destChannel);
        port = target.valid() ? target.paramName
                              : jackLabel(processor, visibleJackOf(processor, cable.id.dstPort, true), true);
    } else {
        const int raw = farIsDestination ? cable.id.dstPort : cable.id.srcPort;
        port = jackLabel(processor, visibleJackOf(processor, raw, farIsDestination), farIsDestination);
    }
    return editor.getModuleTitle(id, processor) + dotSeparator() + port;
}

bool landsOn(GraphEditor& editor, const GraphEditor::VisibleCable& cable, const PortRef& port) {
    if ((cable.signal == CableSignal::Midi) != port.isMidi)
        return false;
    const uint32_t uid = port.isInput ? cable.id.dstUid : cable.id.srcUid;
    if (uid != port.node.uid)
        return false;
    if (port.isMidi)
        return true;
    const int raw = port.isInput ? cable.id.dstPort : cable.id.srcPort;
    return visibleJackOf(processorFor(editor, port.node), raw, port.isInput) == port.jack;
}

} // namespace

std::vector<PortConnection> listPortConnections(GraphEditor& editor, const PortRef& port) {
    std::vector<PortConnection> out;
    const auto cables = editor.buildVisibleCables(); // a copy: the editor's list is rebuilt under us by an edit
    for (const auto& cable : cables) {
        if (!landsOn(editor, cable, port))
            continue;
        PortConnection entry;
        entry.cable = cable;
        entry.label = endName(editor, cable, /*farIsDestination=*/!port.isInput);
        entry.colour = editor.colourForCable(cable);
        out.push_back(std::move(entry));
    }
    return out;
}

juce::String portTitle(GraphEditor& editor, const PortRef& port) {
    auto* processor = processorFor(editor, port.node);
    if (processor == nullptr)
        return {};
    if (const auto owner = editor.getMacroController().macroPortOwnerFor(port.node); owner.port != nullptr)
        return owner.port->name;
    const auto name = port.isMidi ? midiLabel(port.isInput) : jackLabel(processor, port.jack, port.isInput);
    return editor.getModuleTitle(port.node, processor) + dotSeparator() + name;
}

juce::String connectionCountText(int count) {
    if (count <= 0)
        return "No connections yet";
    return count == 1 ? juce::String("1 connection") : juce::String(count) + " connections";
}

} // namespace synth::ui
