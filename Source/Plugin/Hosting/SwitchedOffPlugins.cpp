#include "Plugin/Hosting/SwitchedOffPlugins.h"
#include "Modules/ChannelStripModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include <algorithm>
#include <set>
#include <vector>

namespace synth {

std::vector<SwitchedOffPlugin> switchedOffPluginsFeeding(juce::AudioProcessorGraph& graph,
                                                         juce::AudioProcessorGraph::NodeID strip) {
    std::vector<SwitchedOffPlugin> found;
    if (graph.getNodeForId(strip) == nullptr)
        return found;
    // The common case (nothing switched off) costs one pass over the nodes, not a copy of every cable.
    const auto anySwitchedOff = [&graph] {
        for (auto* node : graph.getNodes())
            if (auto* hosted = dynamic_cast<HostedPluginModule*>(node->getProcessor());
                hosted != nullptr && hosted->isSwitchedOffAfterCrash())
                return true;
        return false;
    };
    if (!anySwitchedOff())
        return found;
    const auto connections = graph.getConnections();
    std::set<juce::uint32> seen{strip.uid};
    std::vector<juce::AudioProcessorGraph::NodeID> pending{strip};
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        for (const auto& c : connections) {
            if (c.destination.nodeID != current || !seen.insert(c.source.nodeID.uid).second)
                continue;
            auto* node = graph.getNodeForId(c.source.nodeID);
            if (node == nullptr)
                continue;
            auto* processor = node->getProcessor();
            if (dynamic_cast<ChannelStripModule*>(processor) != nullptr)
                continue; // another channel: its own plugins belong to it
            if (auto* hosted = dynamic_cast<HostedPluginModule*>(processor);
                hosted != nullptr && hosted->isSwitchedOffAfterCrash())
                found.push_back({hosted->getDefaultTitle(), node->properties["uuid"].toString()});
            pending.push_back(c.source.nodeID);
        }
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.title.compareNatural(b.title) < 0; });
    return found;
}

juce::StringArray titlesOf(const std::vector<SwitchedOffPlugin>& plugins) {
    juce::StringArray titles;
    for (const auto& p : plugins)
        titles.add(p.title);
    return titles;
}

} // namespace synth
