// Concern: runtime state accessors — voice count, master mute, transport enable, input monitoring, automation slicing
// and the feedback guard's one-shot trip report.

#include "AudioEngine.h"
#include "Modules/PolyMidiModule.h"
#include <bit>

AudioEngine::VoiceInfo AudioEngine::getActiveVoiceInfo() const {
    VoiceInfo info;
    for (auto* node : mainProcessorGraph.getNodes()) {
        if (auto* pm = dynamic_cast<PolyMidiModule*>(node->getProcessor())) {
            info.maxVoices += 8;
            info.activeVoices += static_cast<int>(std::popcount(static_cast<unsigned>(pm->getActiveVoiceMask())));
        }
    }
    return info;
}

int AudioEngine::getDisplayVoiceCount() const { return getActiveVoiceInfo().activeVoices; }

void AudioEngine::setMasterMute(bool muted) noexcept { masterMuted_.store(muted, std::memory_order_relaxed); }

bool AudioEngine::isMasterMuted() const noexcept { return masterMuted_.load(std::memory_order_relaxed); }

void AudioEngine::setTransportEnabled(bool enabled) noexcept {
    transportEnabled_.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::isTransportEnabled() const noexcept { return transportEnabled_.load(std::memory_order_relaxed); }

void AudioEngine::setInputMonitoringEnabled(bool enabled) noexcept {
    inputMonitoringEnabled_.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::isInputMonitoringEnabled() const noexcept {
    return inputMonitoringEnabled_.load(std::memory_order_relaxed);
}

// An atomic exchange back to false in the same call, so a caller that polls (MainComponent's 10 Hz
// timer) consumes a trip exactly once however many ticks pass before it reads it.
bool AudioEngine::consumeFeedbackGuardTripped() noexcept {
    return feedbackGuardTripped_.exchange(false, std::memory_order_relaxed);
}

// Run the whole per-block sequence (transport tick, snapshot open, MIDI capture, automation apply,
// graph render) once per 64-sample slice instead of once per callback, so block-rate automation
// becomes control-rate automation.
//
// Default OFF, and it must stay that way until measured per patch: slicing is not audio-neutral.
// Time-invariant processing doesn't care about block size, but anything with a per-block LFO update
// or an FFT hop (Chorus, Phaser, PitchShifter …) renders audibly differently at 64 samples than at
// 512 — see AutomationSlicingTest.SliceParityTimeInvariantChain, which measures exactly that
// difference. It also multiplies the per-block overhead (graph traversal, playhead re-application,
// transport tick) by blockSize/64.
void AudioEngine::setAutomationSlicingEnabled(bool enabled) noexcept {
    automationSlicingEnabled_.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::isAutomationSlicingEnabled() const noexcept {
    return automationSlicingEnabled_.load(std::memory_order_relaxed);
}
