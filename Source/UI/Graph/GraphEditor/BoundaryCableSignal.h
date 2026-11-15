#pragma once

// What a plain cable into a macro's boundary port (Macro In / Macro Out) carries. The port itself is a pass-through
// with Audio-role jacks, so it cannot say; the signal is whatever enters it, followed back through any chain of
// ports. docs/layout/cables.md#a-cable-into-a-port-carries-its-sources-signal.

#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CableColour.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace synth::ui {

inline bool isMacroBoundaryPort(const juce::AudioProcessor* p) {
    return dynamic_cast<const MacroInletModule*>(p) != nullptr || dynamic_cast<const MacroOutletModule*>(p) != nullptr;
}

// True when the signal leaving `node`'s raw output `channel` is a modulation signal: a ModCV output, or a boundary
// port fed (through any number of ports) by one. `depth` bounds a malformed cycle of ports.
inline bool carriesModulation(const juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID node,
                              int channel, int depth = 0) {
    const auto* found = graph.getNodeForId(node);
    if (found == nullptr || depth > 16)
        return false;
    const auto* processor = found->getProcessor();
    if (isMacroBoundaryPort(processor)) {
        for (const auto& c : graph.getConnections())
            if (c.destination.nodeID == node && c.destination.channelIndex == channel)
                return carriesModulation(graph, c.source.nodeID, c.source.channelIndex, depth + 1);
        return false;
    }
    const auto* module = dynamic_cast<const ModuleBase*>(processor);
    return module != nullptr && module->mapOutputChannel(channel).role == PortRole::ModCV;
}

// The signal of a plain (non-MIDI) cable from `source` into `dest`: a modulation signal entering a boundary port is
// drawn as the mod cable it is; everything else is audio.
inline CableSignal plainCableSignal(const juce::AudioProcessorGraph& graph,
                                    const juce::AudioProcessorGraph::Connection& c) {
    const auto* dest = graph.getNodeForId(c.destination.nodeID);
    if (dest != nullptr && isMacroBoundaryPort(dest->getProcessor()) &&
        carriesModulation(graph, c.source.nodeID, c.source.channelIndex))
        return CableSignal::ModCV;
    return CableSignal::Audio;
}

// A Direct cable's signal: MIDI when either end is a MIDI jack, else plainCableSignal.
inline CableSignal directCableSignal(const juce::AudioProcessorGraph& graph,
                                     const juce::AudioProcessorGraph::Connection& c, bool midi) {
    return midi ? CableSignal::Midi : plainCableSignal(graph, c);
}

} // namespace synth::ui
