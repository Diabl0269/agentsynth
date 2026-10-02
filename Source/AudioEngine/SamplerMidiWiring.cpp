#include "SamplerMidiWiring.h"

#include "../Modules/SamplerModule.h"
#include <set>

namespace synth {

// One scan of the connections, then one atomic store per Sampler; the audio thread reads it once per
// block. JUCE's graph never says which inputs of a processor are connected, so this is read off the
// connection list.
void publishSamplerMidiWiring(juce::AudioProcessorGraph& graph) {
    std::set<juce::AudioProcessorGraph::NodeID> midiFed;
    for (const auto& connection : graph.getConnections())
        if (connection.destination.isMIDI())
            midiFed.insert(connection.destination.nodeID);

    for (auto* node : graph.getNodes())
        if (auto* sampler = node != nullptr ? dynamic_cast<SamplerModule*>(node->getProcessor()) : nullptr)
            sampler->setMidiInputWired(midiFed.count(node->nodeID) != 0);
}

} // namespace synth
