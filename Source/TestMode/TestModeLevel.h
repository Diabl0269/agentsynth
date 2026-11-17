#pragma once

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include <string>
#include <vector>

// TestModeLevel.h: the level maths behind `audioPump {levels: ...}` (docs/development/test-mode.md#audio-pump): the
// RMS and peak of rendered blocks, and the verdict on "louder as velocity goes up". Header-only and device-free so a
// test can run it without a window.
namespace synth::testmode {

struct LevelReading {
    double rms = 0.0;
    double peak = 0.0;
};

/** Accumulates RMS and peak over every channel of the blocks it is given. */
class LevelMeter {
public:
    void add(const juce::AudioBuffer<float>& block) {
        for (int ch = 0; ch < block.getNumChannels(); ++ch) {
            const float* data = block.getReadPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i) {
                const double s = data[i];
                sumSquares += s * s;
                peak = std::max(peak, std::abs(s));
            }
            count += block.getNumSamples();
        }
    }

    LevelReading reading() const { return {count > 0 ? std::sqrt(sumSquares / (double)count) : 0.0, peak}; }

private:
    double sumSquares = 0.0;
    double peak = 0.0;
    long long count = 0;
};

/** Empty when every level is above `floor` and each is strictly above the one before; else says which one is not. */
inline std::string risingVerdict(const std::vector<int>& velocities, const std::vector<double>& rms, double floor) {
    for (size_t i = 0; i < rms.size() && i < velocities.size(); ++i) {
        if (!(rms[i] > floor))
            return "velocity " + std::to_string(velocities[i]) + " is silent: rms " + std::to_string(rms[i]) +
                   " is not above " + std::to_string(floor);
        if (i > 0 && !(rms[i] > rms[i - 1]))
            return "velocity " + std::to_string(velocities[i]) + " (rms " + std::to_string(rms[i]) +
                   ") is not louder than velocity " + std::to_string(velocities[i - 1]) + " (rms " +
                   std::to_string(rms[i - 1]) + ")";
    }
    return {};
}

} // namespace synth::testmode
