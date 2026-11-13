// LoopStretch.cpp -- tempo and key matching for loops: the ratio and key maths, and the Signalsmith Stretch call.
#include "SoundBrowser/LoopStretch.h"
#include "Music/SongKey.h"
#include "SoundBrowser/SoundSearch.h"
#include <cmath>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <vector>

namespace synth::soundbrowser {

namespace {
constexpr double kNoStretchTolerance = 0.005;
constexpr double kMinStretchSeconds = 0.25;
} // namespace

double LoopTarget::tempoRatio() const noexcept { return fileBpm > 0.0 && songBpm > 0.0 ? songBpm / fileBpm : 1.0; }

bool LoopTarget::needsStretch() const noexcept {
    return semitones != 0 || std::abs(tempoRatio() - 1.0) > kNoStretchTolerance;
}

double bestStretchBpm(double fileBpm, double songBpm) {
    if (fileBpm <= 0.0 || songBpm <= 0.0)
        return fileBpm;
    double best = fileBpm;
    for (const double candidate : {fileBpm * 2.0, fileBpm * 0.5})
        if (std::abs(candidate - songBpm) < std::abs(best - songBpm))
            best = candidate;
    return best;
}

namespace {

// "D minor" / "F major"; a bare root keeps whatever mode is known.
juce::String keyName(int pitchClass, int mode) {
    return synth::keyRootName(pitchClass) + (mode == 2 ? " minor" : mode == 1 ? " major" : "");
}

} // namespace

KeyPlan planKeyMatch(const juce::String& loopKey, const juce::String& songKey) {
    const auto loop = parseSoundKey(loopKey);
    const auto song = parseSoundKey(songKey);
    KeyPlan plan;
    if (song.pitchClass < 0)
        return plan;
    if (loop.pitchClass < 0) {
        plan.keyMatch = KeyMatch::NoLoopKey;
        return plan;
    }
    int targetRoot = song.pitchClass;
    int targetMode = song.mode != 0 ? song.mode : loop.mode;
    if (loop.mode != 0 && song.mode != 0 && loop.mode != song.mode) {
        // The relative key shares the notes: a minor key's relative major is 3 semitones up, a major key's minor 3
        // down.
        targetRoot = (song.pitchClass + (song.mode == 2 ? 3 : 9)) % 12;
        targetMode = loop.mode;
    }
    int delta = ((targetRoot - loop.pitchClass) % 12 + 12) % 12;
    if (delta > 6)
        delta -= 12;
    plan.semitones = delta;
    plan.keyMatch = delta == 0 ? KeyMatch::InKey : KeyMatch::Shifted;
    plan.targetKey = keyName(targetRoot, targetMode);
    return plan;
}

int semitonesToKey(const juce::String& loopKey, const juce::String& songKey) {
    return planKeyMatch(loopKey, songKey).semitones;
}

LoopTarget makeLoopTarget(double entryBpm, const juce::String& entryKey, double songBpm, const juce::String& songKey,
                          bool matchKey) {
    LoopTarget target;
    target.songBpm = songBpm;
    target.fileBpm = bestStretchBpm(entryBpm, songBpm);
    if (matchKey) {
        const auto plan = planKeyMatch(entryKey, songKey);
        target.semitones = plan.semitones;
        target.keyMatch = plan.keyMatch;
        target.targetKey = plan.targetKey;
    }
    return target;
}

juce::String soundKeyForScale(const std::optional<synth::MusicalScale>& key) {
    if (!key.has_value())
        return {};
    // Bit 3 is the minor third, bit 4 the major third (the mask is root-relative).
    const bool minorThird = (key->mask & (1u << 3)) != 0;
    const bool majorThird = (key->mask & (1u << 4)) != 0;
    if (minorThird == majorThird)
        return {};
    return synth::keyRootName(key->rootPitchClass) + (minorThird ? " minor" : " major");
}

juce::AudioBuffer<float> stretchLoop(const juce::AudioBuffer<float>& in, double sampleRate, double tempoRatio,
                                     double semitones) {
    const int channels = in.getNumChannels();
    const int length = in.getNumSamples();
    const bool unchanged = std::abs(tempoRatio - 1.0) < 1.0e-9 && std::abs(semitones) < 1.0e-9;
    if (tempoRatio <= 0.0 || unchanged || channels < 1 || sampleRate <= 0.0 ||
        (double)length < kMinStretchSeconds * sampleRate)
        return in;

    const int outLength = juce::jmax(1, juce::roundToInt((double)length / tempoRatio));
    juce::AudioBuffer<float> out(channels, outLength);
    out.clear();

    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault(channels, (float)sampleRate);
    stretch.setTransposeSemitones((float)semitones);

    std::vector<const float*> inputs((size_t)channels);
    std::vector<float*> outputs((size_t)channels);
    for (int ch = 0; ch < channels; ++ch) {
        inputs[(size_t)ch] = in.getReadPointer(ch);
        outputs[(size_t)ch] = out.getWritePointer(ch);
    }
    if (!stretch.exact(inputs.data(), length, outputs.data(), outLength))
        return in;
    return out;
}

} // namespace synth::soundbrowser
