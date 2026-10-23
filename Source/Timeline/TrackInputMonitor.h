#pragma once

#include "Timeline/TimelineSnapshot.h"
#include "Timeline/TrackAudioInput.h"
#include "Transport/TransportService.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>

namespace synth {

// Thresholds a track's monitoring feedback guard trips on: the monitored input's block peak at or above
// kMonitorGuardPeakThreshold for kMonitorGuardSustainSeconds of samples in a row. The engine-wide guard uses the same
// pair (AudioEngine::kFeedbackPeakThreshold / kFeedbackSustainSeconds).
inline constexpr float kMonitorGuardPeakThreshold = 0.97f;
inline constexpr double kMonitorGuardSustainSeconds = 0.25;

// One audio track's live input: mixes the track's own device inputs into its Track Audio node's output while the
// monitor rule says the track hears them, and mutes it for good when the input clips for too long (feedback). Owned
// by the node; process() runs on the AUDIO THREAD inside the node's processBlock, allocation- and lock-free.
class TrackInputMonitor {
public:
    // Adds the input to `buffer` (channels 0 and 1; a mono input goes to both) when `track` should hear it. Returns
    // whether anything was added. Call it every block the track is not silenced, playing or not.
    bool process(const TransportService& transport, const TimelineSnapshot::TrackInfo& track, bool playing,
                 juce::AudioBuffer<float>& buffer) noexcept {
        noteArmed(track.armed);
        const auto& context = transport.getInputMonitorContext();
        if (!hearsInput(track, playing, context.recording) || !onOpenDevice(track, context)) {
            run_ = 0;
            return false;
        }
        const float* left = transport.getDeviceInputForDeviceChannel(track.inputFirstChannel);
        const float* right =
            track.inputNumChannels > 1 ? transport.getDeviceInputForDeviceChannel(track.inputFirstChannel + 1) : left;
        const int samples = std::min(buffer.getNumSamples(), transport.getNumDeviceInputSamples());
        if (latched_.load(std::memory_order_relaxed) || (left == nullptr && right == nullptr) || samples <= 0 ||
            buffer.getNumChannels() <= 0)
            return false;
        if (tripsGuard(left, right, samples, transport.getCurrentBlockInfo().sampleRate)) {
            if (context.guardReports != nullptr)
                context.guardReports->report(track.trackId);
            return false;
        }
        addInto(buffer, 0, left, samples);
        if (buffer.getNumChannels() > 1)
            addInto(buffer, 1, right, samples);
        return true;
    }

    // Any thread. Whether sustained clipping has muted this track's monitoring (re-arming the track clears it).
    bool isGuardLatched() const noexcept { return latched_.load(std::memory_order_relaxed); }
    // The silenced paths (bypass, mute, no track) call this so a run never spans them.
    void resetRun() noexcept { run_ = 0; }

private:
    static bool hearsInput(const TimelineSnapshot::TrackInfo& track, bool playing, bool recording) noexcept {
        return track.inputNumChannels > 0 &&
               monitorHearsInput(static_cast<MonitorMode>(track.monitorMode), track.armed, playing, recording);
    }

    // A choice picked on another device plays nothing, like it records nothing (isAudioInputConnected).
    static bool onOpenDevice(const TimelineSnapshot::TrackInfo& track,
                             const TransportService::InputMonitorContext& context) noexcept {
        return track.inputDeviceHash == 0 || context.inputDeviceHash == 0 ||
               track.inputDeviceHash == context.inputDeviceHash;
    }

    // Re-arming (the armed flag rising) is the reset gesture, as it was for the old engine-wide gate.
    void noteArmed(bool armed) noexcept {
        if (armed && !wasArmed_)
            latched_.store(false, std::memory_order_relaxed);
        wasArmed_ = armed;
    }

    // True on the block the guard trips (that block is not played either).
    bool tripsGuard(const float* left, const float* right, int samples, double sampleRate) noexcept {
        float peak = 0.0f;
        for (const float* channel : {left, right})
            if (channel != nullptr)
                for (int i = 0; i < samples; ++i)
                    peak = std::max(peak, std::abs(channel[i]));
        if (peak < kMonitorGuardPeakThreshold) {
            run_ = 0;
            return false;
        }
        run_ += samples;
        if (!(sampleRate > 0.0) || (double)run_ / sampleRate < kMonitorGuardSustainSeconds)
            return false;
        latched_.store(true, std::memory_order_relaxed);
        run_ = 0;
        return true;
    }

    static void addInto(juce::AudioBuffer<float>& buffer, int channel, const float* source, int samples) noexcept {
        if (source != nullptr)
            buffer.addFrom(channel, 0, source, samples);
    }

    std::int64_t run_ = 0; // audio thread: samples in a row at or above the threshold
    bool wasArmed_ = false;
    std::atomic<bool> latched_{false};
};

} // namespace synth
