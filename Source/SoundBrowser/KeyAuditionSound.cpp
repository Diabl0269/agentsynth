// KeyAuditionSound.cpp -- the default scale sound: a sine with a little second and third harmonic, a 4 ms attack, an
// exponential decay and a short linear release at the end of each note's slot so no note ends on a step.
#include "SoundBrowser/KeyAuditionSound.h"
#include "SoundBrowser/SoundPreviewPlayer.h"
#include <cmath>

namespace synth::soundbrowser {

namespace {
constexpr double kAttackSeconds = 0.004;
constexpr double kReleaseSeconds = 0.015;
constexpr double kDecaySeconds = 0.07;
constexpr double kLastNoteSteps = 3.0; // the final note's slot, in steps, so it can ring out
constexpr float kPeak = 0.22f;         // the sum of the three partials is normalised to this

double noteFrequency(int pitch) { return 440.0 * std::pow(2.0, (pitch - 69) / 12.0); }

float noteEnvelope(double t, double length) {
    const double attack = std::min(1.0, t / kAttackSeconds);
    const double decay = std::exp(-t / kDecaySeconds);
    const double release = std::min(1.0, std::max(0.0, (length - t) / kReleaseSeconds));
    return (float)(attack * decay * release);
}
} // namespace

juce::AudioBuffer<float> renderKeyAuditionSound(const std::vector<int>& pitches, double sampleRate) {
    if (pitches.empty() || !(sampleRate > 0.0))
        return {};
    const double stepSeconds = kKeyAuditionStepMs / 1000.0;
    const double totalSeconds = stepSeconds * ((double)pitches.size() - 1.0 + kLastNoteSteps);
    const int total = (int)std::ceil(totalSeconds * sampleRate);
    juce::AudioBuffer<float> out(2, total);
    out.clear();
    auto* mono = out.getWritePointer(0);
    constexpr double kTwoPi = 6.283185307179586;
    const float norm = kPeak / 1.35f;
    for (size_t n = 0; n < pitches.size(); ++n) {
        const double length = stepSeconds * (n + 1 == pitches.size() ? kLastNoteSteps : 1.0);
        const int first = (int)std::llround((double)n * stepSeconds * sampleRate);
        const int count = std::min(total - first, (int)std::floor(length * sampleRate));
        const double omega = kTwoPi * noteFrequency(pitches[n]) / sampleRate;
        for (int i = 0; i < count; ++i) {
            const double phase = omega * i;
            const float tone = (float)(std::sin(phase) + 0.25 * std::sin(2.0 * phase) + 0.1 * std::sin(3.0 * phase));
            mono[first + i] += tone * noteEnvelope((double)i / sampleRate, length) * norm;
        }
    }
    out.copyFrom(1, 0, out, 0, 0, total);
    return out;
}

void playKeyAuditionSound(SoundPreviewPlayer& player, const std::vector<int>& pitches) {
    player.playRendered(renderKeyAuditionSound(pitches, player.getSampleRate()));
}

} // namespace synth::soundbrowser
