#pragma once

#include "HostedPluginModule.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <utility>
#include <vector>

namespace synth {

/** User setting (default true): unload hosted plugins while the Mac sleeps. */
inline constexpr const char* kUnloadPluginsDuringSleepKey = "unloadPluginsDuringSleep";

/** Decides what a sleep and a wake do to the hosted plugins: every in-process instance is saved and freed before
 *  sleep, and on wake each is loaded again as it was, with its editor window reopened if one was open. Pure logic
 *  over Hooks, so tests drive it without an OS; SystemSleepWatcher feeds it in the app. Message thread only. */
class HostedPluginSleepGuard {
public:
    struct Hooks {
        std::function<std::vector<std::pair<juce::AudioProcessorGraph::NodeID, HostedPluginModule*>>()> hostedModules;
        std::function<bool(juce::AudioProcessorGraph::NodeID)> isEditorOpen;
        std::function<void(juce::AudioProcessorGraph::NodeID)> closeEditor;
        std::function<void(HostedPluginModule*, juce::AudioProcessorGraph::NodeID)> openEditor;
        std::function<bool()> enabled; // read at every sleep, so the setting takes effect at once
    };

    explicit HostedPluginSleepGuard(Hooks hooks);
    ~HostedPluginSleepGuard();

    HostedPluginSleepGuard(const HostedPluginSleepGuard&) = delete;
    HostedPluginSleepGuard& operator=(const HostedPluginSleepGuard&) = delete;

    /** Puts every module that can sleep to sleep (nothing when the setting is off). Returns how many it put to sleep.
     */
    int systemWillSleep();
    /** Reloads what systemWillSleep() put to sleep, even if the setting was turned off meanwhile. Runs after the wake
     *  delay (0 = now); a sleep that comes first cancels it. */
    void systemDidWake();
    /** Milliseconds between the wake and the reload, so USB devices can enumerate again. */
    void setWakeDelayMs(int delayMs) noexcept { wakeDelayMs_ = delayMs < 0 ? 0 : delayMs; }
    int getWakeDelayMs() const noexcept { return wakeDelayMs_; }
    /** Modules put to sleep and not yet reloaded. */
    int getAsleepCount() const { return static_cast<int>(asleep_.size()); }

private:
    class ReopenObserver;
    struct Sleeper {
        juce::AudioProcessorGraph::NodeID id;
        juce::WeakReference<HostedPluginModule> module;
        bool editorWasOpen = false;
    };

    void wakeNow();

    Hooks hooks_;
    std::vector<Sleeper> asleep_;
    std::vector<std::unique_ptr<ReopenObserver>> observers_;
    int wakeDelayMs_ = 2000;
    int wakeGeneration_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace synth
