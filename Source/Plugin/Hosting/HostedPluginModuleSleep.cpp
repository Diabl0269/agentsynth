// Concern: HostedPluginModule's sleep and wake -- unloading the plugin before the Mac sleeps and reloading it after.
//
// A plugin runs its own threads inside this process (a USB hotplug thread, say), and one of them that trips over
// the Mac sleeping takes the whole app down. So before sleep every in-process instance is saved and freed, and on
// wake it is loaded again through the same path a Turn on uses, which keeps the saved state, card layout, mod slots
// and chosen outputs. An out-of-process instance (an AUv3) cannot hurt this process and is left alone.
#include "HostedPluginModule.h"

#if JUCE_MAC && JUCE_PLUGINHOST_AU
#include <AudioToolbox/AudioToolbox.h>
#endif

namespace synth {

namespace {
/** How long suspendForSleep() waits for the audio thread to let go of the retired instance. Past it the instance is
 *  left to the ordinary reap timer: a stopped audio thread is not holding it, but nothing can prove that. */
constexpr int kSleepReapTimeoutMs = 250;

std::function<bool(juce::AudioPluginInstance&)>& outOfProcessProbeOverride() {
    static std::function<bool(juce::AudioPluginInstance&)> probe;
    return probe;
}
} // namespace

void HostedPluginModule::setOutOfProcessProbeForTest(std::function<bool(juce::AudioPluginInstance&)> probe) {
    outOfProcessProbeOverride() = std::move(probe);
}

// Same test JUCE's AudioUnit format uses to decide an AUv3 (which it instantiates out of process).
bool HostedPluginModule::isOutOfProcessInstance(juce::AudioPluginInstance& instance) {
    if (const auto& probe = outOfProcessProbeOverride())
        return probe(instance);
#if JUCE_MAC && JUCE_PLUGINHOST_AU
    if (auto* client = instance.getAudioUnitClient()) {
        const AudioUnit audioUnit = client->getAudioUnitHandle();
        if (audioUnit == nullptr)
            return false;
        AudioComponentDescription description{};
        if (auto component = AudioComponentInstanceGetComponent(audioUnit))
            if (AudioComponentGetDescription(component, &description) == noErr)
                return (description.componentFlags & kAudioComponentFlag_IsV3AudioUnit) != 0;
    }
#endif
    return false;
}

bool HostedPluginModule::canSuspendForSleep() const {
    if (suspendedForSleep_)
        return false;
    if (auto* instance = activeInstance_.load(std::memory_order_acquire))
        return !isOutOfProcessInstance(*instance);
    return loading_ && identity_.isValid();
}

bool HostedPluginModule::suspendForSleep() {
    if (!canSuspendForSleep())
        return false;

    if (auto* instance = activeInstance_.load(std::memory_order_acquire)) {
        // Keep what a re-save would write, so the reload (and a save made while asleep) loses nothing.
        pendingBlob_.reset();
        instance->getStateInformation(pendingBlob_);
        savedOutputs_ = outputsToVar();
        outputsRestored_ = true;

        ++loadGeneration_;
        loading_ = false;
        retireActiveInstance();

        // Free it now: the point is that none of its code runs during sleep. A retired instance is only reapable
        // once the audio thread has started later blocks.
        const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(kSleepReapTimeoutMs);
        while (!retired_.empty() && juce::Time::getMillisecondCounter() < deadline) {
            reapRetired();
            if (!retired_.empty())
                juce::Thread::sleep(1);
        }
    } else {
        ++loadGeneration_; // the load's callback now finds itself superseded and drops its instance
        loading_ = false;
    }

    suspendedForSleep_ = true;
    return true;
}

void HostedPluginModule::resumeAfterSleep(HostedPluginBackend& backend) {
    if (!suspendedForSleep_)
        return;
    suspendedForSleep_ = false;
    if (identity_.isValid() && !hasInstance() && !loading_)
        startIdentityLoad(identity_, backend);
}

} // namespace synth
