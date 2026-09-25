#pragma once

#include "Plugin/Hosting/HostedPluginModule.h"
#include <array>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::ui {

/**
 * "Touch in the plugin editor to add" (docs/control/plugin-card-layout.md#choosing-knobs).
 *
 * Two signals feed the same owner-facing callback: a gesture START
 * (`parameterGestureChanged(index, true)`, the original FRO132 path -- most plugins) and, for a
 * plugin that never emits one, a VALUE CHANGE on a parameter not already in the layout (FRO241). The
 * value-change path is burst-filtered so an automation sweep or a preset load -- which can touch many
 * parameters near-simultaneously -- does not add all of them: see `handleAsyncUpdate`'s doc comment.
 *
 * Registers itself as a Listener on EVERY current parameter of the module's live instance (there is
 * no "which parameter" to scope to up front -- any of them might be the one touched), which is why
 * this is its own class rather than a one-off on HostedParameterAttachment (which binds exactly one
 * parameter). `juce::AudioProcessorParameter::Listener` callbacks may arrive on ANY thread, INCLUDING
 * the audio thread (a `parameterValueChanged` for automation arrives from `processHostBlock`) -- so,
 * like every other hosted-parameter listener in this codebase (HostedParameterAttachment,
 * HostedPluginModule::InstanceListener), a callback here only queues, cheaply and without allocating,
 * and hops via `juce::AsyncUpdater`; `onParameterTouched` is never called off the message thread.
 * Message thread only otherwise (construction, setArmed, destruction, the burst timer).
 */
class PluginKnobPickerTouchCapture final
    : private juce::AudioProcessorParameter::Listener
    , private juce::AsyncUpdater
    , private juce::Timer {
public:
    explicit PluginKnobPickerTouchCapture(HostedPluginModule& module);
    ~PluginKnobPickerTouchCapture() override;

    /** Registers/unregisters a listener on every parameter of the CURRENT live instance (a no-op
     *  with none) and, when arming, calls `onRequestOpenEditor` once. Idempotent. Disarming also
     *  cancels any value-change burst window still waiting to close, and any already-queued gesture
     *  update -- nothing reported before this call can fire after it returns. */
    void setArmed(bool armed);
    bool isArmed() const noexcept { return armed_; }

    /** Message thread. One live parameter's index, reported once a gesture-start or a value-change
     *  fallback candidate has been hopped here -- never called from parameterGestureChanged() /
     *  parameterValueChanged() themselves (see the class comment). */
    std::function<void(int parameterIndex)> onParameterTouched;
    /** Message thread. Fired once per setArmed(true), so the owner can open the plugin's editor if it
     *  is not already open (HostedPluginWindowManager::openEditorFor is idempotent either way). */
    std::function<void()> onRequestOpenEditor;
    /** Message thread. Set by the owner; asked, per candidate, whether `parameterIndex` is already in
     *  the layout before it is allowed to open (or extend) a value-change burst window -- a value
     *  change on a parameter the layout already shows is the picker's own tick / the card's own knob
     *  attachment moving it, not a new touch. Unset (nullptr) behaves as "no, never in the layout",
     *  which is what a test rig that doesn't care about this filter wants. */
    std::function<bool(int parameterIndex)> isParameterAlreadyInLayout;

    // ---- Test seams: deliver a signal as if it arrived on `parameterIndex`, from whichever thread
    // the caller runs on -- exactly what a real off-thread juce::AudioProcessorParameter callback
    // does, without needing a real parameter wired up. ----
    void simulateGestureStartForTest(int parameterIndex) { parameterGestureChanged(parameterIndex, true); }
    void simulateValueChangeForTest(int parameterIndex) { parameterValueChanged(parameterIndex, 0.0f); }
    /** Closes the value-change burst window right now instead of waiting out the real
     *  `kBurstWindowMs` -- the clock seam for tests: rather than sleeping past the window, a test
     *  drives the same `timerCallback()` a real 200 ms tick would invoke, deterministically and
     *  instantly. A no-op if no window is open. */
    void forceBurstWindowCloseForTest() { timerCallback(); }
    void setBurstWindowMsForTest(int ms) { burstWindowMs_ = ms; } // before arming

private:
    void parameterValueChanged(int parameterIndex, float) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    HostedPluginModule& module_;
    bool armed_ = false;

    juce::CriticalSection queueLock_;
    std::vector<int> pendingIndices_; // guarded by queueLock_; drained on the message thread only

    // ---- Value-change fallback intake: written from ANY thread (incl. audio), drained on the
    // message thread only. A fixed-size array + SpinLock, never a heap allocation, so a
    // processHostBlock-thread automation callback never allocates. ----
    static constexpr int kMaxPendingValueChanges = 32;
    static constexpr int kBurstWindowMs = 200;
    static constexpr int kBurstMaxDistinctParams = 3;

    juce::SpinLock valueChangeLock_;
    std::array<int, kMaxPendingValueChanges> pendingValueChangeIndices_{}; // guarded by valueChangeLock_
    int pendingValueChangeCount_ = 0;                                      // guarded by valueChangeLock_
    bool valueChangeOverflowed_ = false;                                   // guarded by valueChangeLock_

    // ---- Burst window state: message thread only (handleAsyncUpdate / timerCallback). ----
    std::vector<int> burstCandidates_;
    bool burstWindowOpen_ = false;
    int burstWindowMs_ = kBurstWindowMs;
    bool burstExceeded_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginKnobPickerTouchCapture)
};

} // namespace synth::ui
