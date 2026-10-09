// MainComponentTrackModule.cpp — "Show Module" for a track (Ctrl+E on a focused track row, and the row's button):
// reveals the track's module on the canvas. A hosted plugin's editor window is its own command, Ctrl+Cmd+E
// (toggleTrackPluginWindow).
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
// itself; null for an unbound or orphaned track.
juce::AudioProcessorGraph::Node* MainComponent::trackModuleNode(synth::TrackId trackId) {
    const auto* track = timelineDoc.getTrack(trackId);
    if (track == nullptr || track->orphaned)
        return nullptr;
    auto* bound = findNodeByUuid(track->bindingUuid);
    if (bound == nullptr)
        return nullptr;
    auto* instrument = instrumentOf(audioEngine.getGraph(), *bound);
    return instrument != nullptr ? instrument : bound;
}

// Selects and centres the module; it never opens a window (that is toggleTrackPluginWindow's job).
void MainComponent::showTrackModule(synth::TrackId trackId) {
    if (auto* target = trackModuleNode(trackId))
        showNodeOnCanvas(target->properties["uuid"].toString());
}

// Ctrl+Cmd+E: only a hosted plugin has a window, so a built-in instrument or audio track does nothing. Whether the
// window is open is read from the window manager every time, never cached, because the user can close it from its
// own title bar.
void MainComponent::toggleTrackPluginWindow(synth::TrackId trackId) {
    auto* target = trackModuleNode(trackId);
    if (target == nullptr)
        return;
    if (auto* hosted = dynamic_cast<synth::HostedPluginModule*>(target->getProcessor()))
        pluginWindowManager.toggleEditorFor(hosted, target->nodeID);
}
