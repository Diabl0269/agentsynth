#pragma once

#include "Plugin/Hosting/HostedPluginModule.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::ui {

/**
 * "Touch in the plugin editor to add" (docs/control/plugin-card-layout.md#choosing-knobs).
 *
 * v1 is GESTURE ONLY, a founder decision: while armed, a parameter that reports a gesture START
 * (`juce::AudioProcessorParameter::Listener::parameterGestureChanged(index, true)`) is reported to
 * the owner. The value-change fallback for plugins that never emit gestures (debounced against an
 * automation/preset-load burst) is deferred to FRO241 -- this class exists so that ticket can add it
 * as a second signal into the SAME queue/hop below, without touching the armed/unarmed lifecycle or
 * the owner-facing callback. Do not add fallback logic here now.
 *
 * Registers itself as a Listener on EVERY current parameter of the module's live instance (there is
 * no "which parameter" to scope to up front -- any of them might be the one touched), which is why
 * this is its own class rather than a one-off on HostedParameterAttachment (which binds exactly one
 * parameter). `juce::AudioProcessorParameter::Listener` callbacks may arrive on ANY thread -- the
 * plugin's own editor, a controller, automation -- so, like every other hosted-parameter listener in
 * this codebase (HostedParameterAttachment, HostedPluginModule::InstanceListener), a callback here
 * only queues and hops via `juce::AsyncUpdater`; `onParameterTouched` is never called off the message
 * thread. Message thread only otherwise (construction, setArmed, destruction).
 */
class PluginKnobPickerTouchCapture final
    : private juce::AudioProcessorParameter::Listener
    , private juce::AsyncUpdater {
public:
    explicit PluginKnobPickerTouchCapture(HostedPluginModule& module);
    ~PluginKnobPickerTouchCapture() override;

    /** Registers/unregisters a listener on every parameter of the CURRENT live instance (a no-op
     *  with none) and, when arming, calls `onRequestOpenEditor` once. Idempotent. */
    void setArmed(bool armed);
    bool isArmed() const noexcept { return armed_; }

    /** Message thread. One live parameter's index, reported once its gesture-start has been hopped
     *  here -- never called from parameterGestureChanged() itself (see the class comment). */
    std::function<void(int parameterIndex)> onParameterTouched;
    /** Message thread. Fired once per setArmed(true), so the owner can open the plugin's editor if it
     *  is not already open (HostedPluginWindowManager::openEditorFor is idempotent either way). */
    std::function<void()> onRequestOpenEditor;

    // ---- Test seam: deliver a gesture-start as if it arrived on `parameterIndex`, from whichever
    // thread the caller runs on -- exactly what a real off-thread juce::AudioProcessorParameter
    // callback does, without needing a real parameter wired up. ----
    void simulateGestureStartForTest(int parameterIndex) { parameterGestureChanged(parameterIndex, true); }

private:
    void parameterValueChanged(int, float) override {} // v1: gesture only; FRO241 adds a fallback here
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;

    HostedPluginModule& module_;
    bool armed_ = false;

    juce::CriticalSection queueLock_;
    std::vector<int> pendingIndices_; // guarded by queueLock_; drained on the message thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginKnobPickerTouchCapture)
};

} // namespace synth::ui
