// MainComponentPluginSleep.cpp -- the Mac's sleep and wake reaching hosted plugins: builds the
// HostedPluginSleepGuard over this window's graph and editor windows, and the watcher that feeds it.
// The policy lives in Plugin/Hosting/HostedPluginSleepGuard.h; MainComponent is declared in MainComponent.h.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "Plugin/Hosting/HostedPluginSleepGuard.h"
#include "Plugin/Hosting/SystemSleepWatcher.h"

// Called once, by the real app's Main.cpp (never from a constructor), so a test's MainComponent has no watcher.
void MainComponent::startPluginSleepWatch() {
    if (systemSleepWatcher_ != nullptr)
        return;

    synth::HostedPluginSleepGuard::Hooks hooks;
    hooks.hostedModules = [this] {
        std::vector<std::pair<juce::AudioProcessorGraph::NodeID, synth::HostedPluginModule*>> modules;
        for (auto* node : audioEngine.getGraph().getNodes())
            if (auto* module = dynamic_cast<synth::HostedPluginModule*>(node->getProcessor()))
                modules.emplace_back(node->nodeID, module);
        return modules;
    };
    hooks.isEditorOpen = [this](juce::AudioProcessorGraph::NodeID id) { return pluginWindowManager.hasWindowFor(id); };
    hooks.closeEditor = [this](juce::AudioProcessorGraph::NodeID id) { pluginWindowManager.closeAllForNode(id); };
    hooks.openEditor = [this](synth::HostedPluginModule* module, juce::AudioProcessorGraph::NodeID id) {
        pluginWindowManager.openEditorFor(module, id);
    };
    hooks.enabled = [this] {
        auto* settings = appProperties.getUserSettings();
        return settings == nullptr || settings->getBoolValue(synth::kUnloadPluginsDuringSleepKey, true);
    };

    pluginSleepGuard_ = std::make_unique<synth::HostedPluginSleepGuard>(std::move(hooks));
    auto* guard = pluginSleepGuard_.get();
    systemSleepWatcher_ = std::make_unique<synth::SystemSleepWatcher>([guard] { guard->systemWillSleep(); },
                                                                      [guard] { guard->systemDidWake(); });
}
