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

int semitonesToKey(const juce::String& loopKey, const juce::String& songKey) {
    const auto loop = parseSoundKey(loopKey);
    const auto song = parseSoundKey(songKey);
    if (loop.pitchClass < 0 || song.pitchClass < 0)
        return 0;
    if (loop.mode != 0 && song.mode != 0 && loop.mode != song.mode)
        return 0;
    int delta = ((song.pitchClass - loop.pitchClass) % 12 + 12) % 12;
    if (delta > 6)
        delta -= 12;
    return delta;
}

LoopTarget makeLoopTarget(double entryBpm, const juce::String& entryKey, double songBpm, const juce::String& songKey,
                          bool matchKey) {
    LoopTarget target;
    target.songBpm = songBpm;
    target.fileBpm = bestStretchBpm(entryBpm, songBpm);
    target.semitones = matchKey ? semitonesToKey(entryKey, songKey) : 0;
    return target;
}

juce::String soundKeyForScale(const std::optional<synth::MusicalScale>& key) {
    if (!key.has_value())
        return {};
    static const char* const kMajorLike[] = {"Major", "Major Pentatonic", "Lydian", "Mixolydian"};
    static const char* const kMinorLike[] = {"Natural Minor", "Minor Pentatonic", "Harmonic Minor",
                                             "Melodic Minor", "Dorian",           "Phrygian"};
    for (const char* name : kMajorLike)
        if (key->name == name)
            return synth::keyRootName(key->rootPitchClass) + " major";
    for (const char* name : kMinorLike)
        if (key->name == name)
            return synth::keyRootName(key->rootPitchClass) + " minor";
    return {};
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
