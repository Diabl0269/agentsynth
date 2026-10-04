// MainComponentTrackModule.cpp — "Show Module" for a track (Ctrl+E on a focused track row, and the row's button):
// reveals the track's module on the canvas and, for a hosted plugin, opens or closes its editor window.
// MainComponent is declared in MainComponent.h; the rest of its implementation lives in the sibling
// MainComponent*.cpp units next to this one.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "MainComponentInternal.h"
#include "Mixer/ChannelFlows/ChannelFlows.h" // findMidiNodesReachedFrom
#include "Plugin/Hosting/HostedPluginModule.h"

namespace {

// The track's instrument: the first node its Track In's MIDI reaches (through macro ports) that is a built-in
// MIDI instrument or a hosted plugin. isMidiInstrumentType does not list ModuleType::HostedPlugin, so the plugin
// is tested by its class. Null when the track has none (an audio track, a Track In wired to nothing yet).
juce::AudioProcessorGraph::Node* instrumentOf(juce::AudioProcessorGraph& graph,
                                              const juce::AudioProcessorGraph::Node& trackIn) {
    for (const auto& leg : synth::findMidiNodesReachedFrom(graph, trackIn.nodeID)) {
        auto* node = graph.getNodeForId(leg.node);
        if (node != nullptr && (dynamic_cast<synth::HostedPluginModule*>(node->getProcessor()) != nullptr ||
                                detail::isMidiInstrumentNode(node->getProcessor())))
            return node;
    }
    return nullptr;
}

} // namespace

// The instrument when there is one (so a Serum track lands on Serum, not on its Track In), else the bound node
// itself. Only a hosted plugin has a window; a built-in instrument just gets its card selected and centred. Whether
// the window is open is read from the window manager every time, never cached, because the user can close it from
// its own title bar. An unbound or orphaned track does nothing.
void MainComponent::showTrackModule(synth::TrackId trackId) {
    const auto* track = timelineDoc.getTrack(trackId);
    if (track == nullptr || track->orphaned)
        return;
    auto* bound = findNodeByUuid(track->bindingUuid);
    if (bound == nullptr)
        return;

    auto& graph = audioEngine.getGraph();
    auto* target = instrumentOf(graph, *bound);
    if (target == nullptr)
        target = bound;
    showNodeOnCanvas(target->properties["uuid"].toString());
    if (auto* hosted = dynamic_cast<synth::HostedPluginModule*>(target->getProcessor()))
        pluginWindowManager.toggleEditorFor(hosted, target->nodeID);
}
