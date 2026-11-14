// Concern: HostedPluginSleepGuard -- the sleep and wake policy for hosted plugins (see the header).
#include "HostedPluginSleepGuard.h"
#include <algorithm>

namespace synth {

// Reopens one node's editor when its reloaded instance goes live, then drops out. Gives up when the reload fails.
class HostedPluginSleepGuard::ReopenObserver : public HostedPluginModule::InstanceObserver {
public:
    ReopenObserver(HostedPluginModule& moduleIn, juce::AudioProcessorGraph::NodeID idIn,
                   std::function<void(HostedPluginModule*, juce::AudioProcessorGraph::NodeID)> openIn)
        : module(&moduleIn)
        , id(idIn)
        , open(std::move(openIn)) {
        moduleIn.addInstanceObserver(this);
    }

    ~ReopenObserver() override { detach(); }

    bool isDone() const noexcept { return module == nullptr; }

    void hostedInstanceGone() override {}

    void hostedInstanceLive() override {
        auto* target = module.get();
        detach();
        if (target != nullptr && open)
            open(target, id);
    }

    void hostedLoadFailed() override { detach(); }

private:
    void detach() {
        if (auto* target = module.get())
            target->removeInstanceObserver(this);
        module = nullptr;
    }

    juce::WeakReference<HostedPluginModule> module;
    juce::AudioProcessorGraph::NodeID id;
    std::function<void(HostedPluginModule*, juce::AudioProcessorGraph::NodeID)> open;
};

HostedPluginSleepGuard::HostedPluginSleepGuard(Hooks hooks)
    : hooks_(std::move(hooks)) {}

HostedPluginSleepGuard::~HostedPluginSleepGuard() {
    *alive_ = false;
    observers_.clear(); // each removes itself from its module if that still exists
}

int HostedPluginSleepGuard::systemWillSleep() {
    ++wakeGeneration_; // a delayed wake that has not run yet is cancelled: the modules stay asleep until the next wake
    if (!hooks_.enabled || !hooks_.enabled() || !hooks_.hostedModules)
        return 0;

    int count = 0;
    for (auto& [id, module] : hooks_.hostedModules()) {
        if (module == nullptr || !module->canSuspendForSleep())
            continue;
        const bool editorWasOpen = hooks_.isEditorOpen && hooks_.isEditorOpen(id);
        if (editorWasOpen && hooks_.closeEditor)
            hooks_.closeEditor(id); // first, so no editor is left standing on the instance that is about to go
        if (!module->suspendForSleep())
            continue;
        ++count;
        const auto known = std::find_if(asleep_.begin(), asleep_.end(), [&](const Sleeper& s) { return s.id == id; });
        if (known != asleep_.end()) {
            known->module = module;
            known->editorWasOpen = known->editorWasOpen || editorWasOpen;
        } else {
            asleep_.push_back({id, juce::WeakReference<HostedPluginModule>(module), editorWasOpen});
        }
    }
    return count;
}

void HostedPluginSleepGuard::systemDidWake() {
    const int generation = ++wakeGeneration_;
    if (asleep_.empty())
        return;
    if (wakeDelayMs_ <= 0) {
        wakeNow();
        return;
    }
    auto alive = alive_;
    juce::Timer::callAfterDelay(wakeDelayMs_, [this, alive, generation]() {
        if (*alive && generation == wakeGeneration_)
            wakeNow();
    });
}

void HostedPluginSleepGuard::wakeNow() {
    observers_.erase(std::remove_if(observers_.begin(), observers_.end(),
                                    [](const std::unique_ptr<ReopenObserver>& o) { return o->isDone(); }),
                     observers_.end());
    auto waking = std::move(asleep_);
    asleep_.clear();
    for (auto& sleeper : waking) {
        auto* module = sleeper.module.get();
        if (module == nullptr)
            continue; // the node was deleted while the Mac slept
        if (sleeper.editorWasOpen)
            observers_.push_back(std::make_unique<ReopenObserver>(*module, sleeper.id, hooks_.openEditor));
        module->resumeAfterSleep();
    }
}

} // namespace synth
