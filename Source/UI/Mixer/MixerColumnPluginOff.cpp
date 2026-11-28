// MixerColumnPluginOff.cpp
//
// The "a plugin on this channel was switched off after a crash" badge in a mixer column's header
// (docs/architecture/plugin-layer.md#when-a-plugin-takes-the-app-down), the twin of the track header's. The column
// asks the graph only when HostedPluginModule::switchedOffRevision() moved (one atomic read per column on the
// existing 10 Hz tick), and the badge fades through the shared PluginOffBadge. Runtime only, never saved.

#include "MixerColumnComponent.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/SwitchedOffPlugins.h"

namespace synth::ui {

void MixerColumnComponent::refreshSwitchedOffPlugins(bool force, bool snap) {
    auto& badge = header_.getPluginOffBadge();
    if (graph_ == nullptr) // unbound ahead of a graph-replacing change: the rebuild asks again
        return;
    const auto revision = synth::HostedPluginModule::switchedOffRevision();
    if (!force && badge.seenRevision == revision)
        return;
    badge.seenRevision = revision;
    const auto plugins = synth::switchedOffPluginsFeeding(*graph_, nodeId_);
    pluginOffUuid_ = plugins.empty() ? juce::String() : plugins.front().nodeUuid;
    header_.setPluginsOff(synth::titlesOf(plugins), snap);
}

} // namespace synth::ui
