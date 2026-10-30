// Loop stretching, measured on rendered audio: tempo (the spacing of clicks), pitch (the measured frequency of a sine),
// the identity case, and the tempo and key arithmetic that decides what a loop is stretched to.
#include "SoundBrowser/LoopStretch.h"
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

using namespace synth::soundbrowser;

namespace {

constexpr double kRate = 44100.0;
constexpr double kPi = juce::MathConstants<double>::pi;

// A 10 ms burst of 1 kHz at the start of every beat.
juce::AudioBuffer<float> clickLoop(double bpm, int beats) {
    const int beatSamples = juce::roundToInt(kRate * 60.0 / bpm);
    juce::AudioBuffer<float> buffer(1, beatSamples * beats);
    buffer.clear();
    const int burst = juce::roundToInt(kRate * 0.01);
    for (int b = 0; b < beats; ++b)
        for (int i = 0; i < burst; ++i) {
            const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * i / burst);
            buffer.setSample(0, b * beatSamples + i, (float)(window * std::sin(2.0 * kPi * 1000.0 * i / kRate)));
        }
    return buffer;
}

// Sample positions where the smoothed envelope first rises past a third of its maximum, at least 0.3 s apart.
std::vector<int> onsets(const juce::AudioBuffer<float>& buffer) {
    const int window = juce::roundToInt(kRate * 0.005);
    const int n = buffer.getNumSamples();
    std::vector<float> envelope((size_t)n, 0.0f);
    float runningMax = 0.0f;
    for (int i = 0; i < n; ++i) {
        float m = 0.0f;
        for (int k = i; k < juce::jmin(n, i + window); ++k)
            m = std::max(m, std::abs(buffer.getSample(0, k)));
        envelope[(size_t)i] = m;
        runningMax = std::max(runningMax, m);
    }
    std::vector<int> found;
    const int refractory = juce::roundToInt(kRate * 0.3);
    for (int i = 0; i < n; ++i)
        if (envelope[(size_t)i] > runningMax / 3.0f && (found.empty() || i - found.back() > refractory))
            found.push_back(i);
    return found;
}

// Frequency from the spacing of rising zero crossings across the middle of the buffer.
double measuredHz(const juce::AudioBuffer<float>& buffer) {
    const int from = buffer.getNumSamples() / 4;
    const int to = buffer.getNumSamples() * 3 / 4;
    double first = -1.0;
    double last = -1.0;
    int count = 0;
    for (int i = from; i < to; ++i) {
        const float a = buffer.getSample(0, i);
        const float b = buffer.getSample(0, i + 1);
        if (a < 0.0f && b >= 0.0f) {
            const double at = i + (double)(-a) / (double)(b - a);
            if (first < 0.0)
                first = at;
            last = at;
            ++count;
        }
    }
    return count < 2 ? 0.0 : (count - 1) * kRate / (last - first);
}

juce::AudioBuffer<float> sine(double hz, double seconds, int channels = 1) {
    juce::AudioBuffer<float> buffer(channels, juce::roundToInt(seconds * kRate));
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(ch, i, 0.5f * (float)std::sin(2.0 * kPi * hz * i / kRate));
    return buffer;
}

} // namespace

TEST(LoopStretchTest, ASlowerTempoLengthensTheLoopAndSpacesTheClicksToMatch) {
    const auto in = clickLoop(120.0, 16); // 4 bars, a click every 0.5 s
    const auto out = stretchLoop(in, kRate, 96.0 / 120.0, 0.0);
    EXPECT_EQ(out.getNumChannels(), 1);
    EXPECT_NEAR((double)out.getNumSamples() / in.getNumSamples(), 1.25, 0.001);

    const auto found = onsets(out);
    ASSERT_GE(found.size(), 12u) << "most of the 16 clicks survive the stretch";
    for (size_t i = 2; i + 2 < found.size(); ++i)
        EXPECT_NEAR((double)(found[i + 1] - found[i]) / kRate, 0.625, 0.010) << "gap " << i;
}

TEST(LoopStretchTest, AFasterTempoShortensTheLoop) {
    const auto in = clickLoop(100.0, 8);
    const auto out = stretchLoop(in, kRate, 1.25, 0.0);
    EXPECT_NEAR((double)out.getNumSamples() / in.getNumSamples(), 0.8, 0.001);
}

TEST(LoopStretchTest, ShiftingTwoSemitonesRaisesAnA440ToB) {
    const auto out = stretchLoop(sine(440.0, 3.0), kRate, 1.0, 2.0);
    EXPECT_EQ(out.getNumSamples(), juce::roundToInt(3.0 * kRate)) << "a shift alone keeps the length";
    EXPECT_NEAR(measuredHz(out), 440.0 * std::pow(2.0, 2.0 / 12.0), 493.9 * 0.01);
}

TEST(LoopStretchTest, TempoAndKeyTogetherKeepBothResults) {
    const auto out = stretchLoop(sine(440.0, 4.0), kRate, 0.8, -3.0);
    EXPECT_NEAR((double)out.getNumSamples() / (4.0 * kRate), 1.25, 0.001);
    EXPECT_NEAR(measuredHz(out), 440.0 * std::pow(2.0, -3.0 / 12.0), 370.0 * 0.01);
}

TEST(LoopStretchTest, NoChangeComesBackIdentical) {
    const auto in = sine(330.0, 1.0, 2);
    const auto out = stretchLoop(in, kRate, 1.0, 0.0);
    ASSERT_EQ(out.getNumSamples(), in.getNumSamples());
    ASSERT_EQ(out.getNumChannels(), 2);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
            ASSERT_EQ(out.getSample(ch, i), in.getSample(ch, i));
}

TEST(LoopStretchTest, ABadRatioOrATinyInputComesBackUnchanged) {
    const auto in = sine(440.0, 1.0);
    EXPECT_EQ(stretchLoop(in, kRate, 0.0, 0.0).getNumSamples(), in.getNumSamples());
    EXPECT_EQ(stretchLoop(in, kRate, -1.0, 0.0).getNumSamples(), in.getNumSamples());
    const auto tiny = sine(440.0, 0.1);
    EXPECT_EQ(stretchLoop(tiny, kRate, 0.5, 0.0).getNumSamples(), tiny.getNumSamples());
}

TEST(LoopStretchTest, StereoKeepsBothChannels) {
    const auto out = stretchLoop(sine(440.0, 1.0, 2), kRate, 0.5, 0.0);
    EXPECT_EQ(out.getNumChannels(), 2);
    EXPECT_NEAR(out.getNumSamples(), 2.0 * kRate, 2.0);
    EXPECT_GT(out.getMagnitude(1, out.getNumSamples() / 4, out.getNumSamples() / 2), 0.2f);
}

TEST(LoopStretchTest, BestStretchBpmPicksTheClosestOfHalfSameAndDouble) {
    EXPECT_DOUBLE_EQ(bestStretchBpm(120.0, 128.0), 120.0);
    EXPECT_DOUBLE_EQ(bestStretchBpm(60.0, 128.0), 120.0) << "a half-time loop plays at double time";
    EXPECT_DOUBLE_EQ(bestStretchBpm(180.0, 90.0), 90.0) << "a double-time loop plays at half time";
    EXPECT_DOUBLE_EQ(bestStretchBpm(100.0, 100.0), 100.0);
    EXPECT_DOUBLE_EQ(bestStretchBpm(0.0, 120.0), 0.0) << "an unknown tempo stays unknown";
    EXPECT_DOUBLE_EQ(bestStretchBpm(120.0, 0.0), 120.0);
}

TEST(LoopStretchTest, SemitonesToKeyTakesTheShortestWayRound) {
    EXPECT_EQ(semitonesToKey("Am", "Dm"), 5);
    EXPECT_EQ(semitonesToKey("Dm", "Am"), -5);
    EXPECT_EQ(semitonesToKey("C", "G"), -5) << "7 up is 5 down";
    EXPECT_EQ(semitonesToKey("C", "F#"), 6);
    EXPECT_EQ(semitonesToKey("C#", "C#"), 0);
    EXPECT_EQ(semitonesToKey("A minor", "C# minor"), 4);
    EXPECT_EQ(semitonesToKey("C major", "A minor"), 0) << "A minor's relative key is C major";
    EXPECT_EQ(semitonesToKey("Am", "D major"), 2) << "D major's relative key is B minor";
    EXPECT_EQ(semitonesToKey("C major", "D minor"), 5) << "D minor's relative key is F major";
    EXPECT_EQ(semitonesToKey("C", "D minor"), 2) << "a bare root has no mode, so it follows the root";
    EXPECT_EQ(semitonesToKey("", "Dm"), 0) << "an unknown loop key";
    EXPECT_EQ(semitonesToKey("Am", ""), 0) << "no song key";
    EXPECT_EQ(semitonesToKey("not a key", "Dm"), 0);
    EXPECT_EQ(semitonesToKey("A", "Dm"), 5) << "a loop of unknown mode follows the root";
}

TEST(LoopStretchTest, ALoopTargetKnowsWhenThereIsNothingToDo) {
    EXPECT_FALSE((LoopTarget{120.0, 120.0, 0}).needsStretch());
    EXPECT_FALSE((LoopTarget{120.0, 120.5, 0}).needsStretch()) << "within 0.5 percent";
    EXPECT_TRUE((LoopTarget{120.0, 124.0, 0}).needsStretch());
    EXPECT_TRUE((LoopTarget{120.0, 120.0, 2}).needsStretch());
    EXPECT_FALSE((LoopTarget{0.0, 120.0, 0}).needsStretch()) << "an unknown file tempo is left alone";
    EXPECT_DOUBLE_EQ((LoopTarget{120.0, 96.0, 0}).tempoRatio(), 0.8);
    EXPECT_DOUBLE_EQ((LoopTarget{0.0, 96.0, 0}).tempoRatio(), 1.0);
}

TEST(LoopStretchTest, MakeLoopTargetFollowsTempoAlwaysAndKeyOnlyWhenAsked) {
    const auto off = makeLoopTarget(120.0, "Am", 96.0, "Dm", false);
    EXPECT_DOUBLE_EQ(off.fileBpm, 120.0);
    EXPECT_DOUBLE_EQ(off.songBpm, 96.0);
    EXPECT_EQ(off.semitones, 0);
    EXPECT_EQ(makeLoopTarget(120.0, "Am", 96.0, "Dm", true).semitones, 5);
    EXPECT_EQ(makeLoopTarget(120.0, "", 96.0, "Dm", true).semitones, 0);
    const auto half = makeLoopTarget(60.0, "", 128.0, "", false);
    EXPECT_DOUBLE_EQ(half.fileBpm, 120.0) << "half time is matched at double time";
    EXPECT_DOUBLE_EQ(makeLoopTarget(0.0, "", 128.0, "", false).fileBpm, 0.0);
}

TEST(LoopStretchTest, ALoopSaysWhyItIsOrIsNotShifted) {
    struct Row {
        const char* what;
        bool matchKey;
        const char* loopKey;
        const char* songKey;
        KeyMatch expected;
        int semitones;
        const char* targetKey;
    };
    const Row rows[] = {
        {"match key off", false, "Am", "Dm", KeyMatch::Off, 0, ""},
        {"no song key", true, "Am", "", KeyMatch::NoSongKey, 0, ""},
        {"no song key, no loop key", true, "", "", KeyMatch::NoSongKey, 0, ""},
        {"no loop key", true, "", "D minor", KeyMatch::NoLoopKey, 0, ""},
        {"a loop key that does not read", true, "not a key", "D minor", KeyMatch::NoLoopKey, 0, ""},
        {"already in key", true, "D minor", "D minor", KeyMatch::InKey, 0, "D minor"},
        {"shifted up", true, "A minor", "D minor", KeyMatch::Shifted, 5, "D minor"},
        {"shifted down", true, "D minor", "A minor", KeyMatch::Shifted, -5, "A minor"},
        {"major loop in a minor song goes to the relative major", true, "C major", "D minor", KeyMatch::Shifted, 5,
         "F major"},
        {"minor loop in a major song goes to the relative minor", true, "A minor", "F major", KeyMatch::Shifted, 5,
         "D minor"},
        {"a major loop in D minor, from below", true, "D major", "D minor", KeyMatch::Shifted, 3, "F major"},
        {"a minor loop in F major, the short way", true, "F minor", "F major", KeyMatch::Shifted, -3, "D minor"},
        {"the relative key is already the loop's", true, "C major", "A minor", KeyMatch::InKey, 0, "C major"},
        {"a bare loop root follows the song's mode", true, "A", "D minor", KeyMatch::Shifted, 5, "D minor"},
    };
    for (const auto& row : rows) {
        const auto target = makeLoopTarget(120.0, row.loopKey, 96.0, row.songKey, row.matchKey);
        EXPECT_EQ(target.keyMatch, row.expected) << row.what;
        EXPECT_EQ(target.semitones, row.semitones) << row.what;
        EXPECT_EQ(target.targetKey, row.targetKey) << row.what;
    }
}

TEST(LoopStretchTest, ALoopNeverMovesMoreThanSixSemitones) {
    for (const char* song : {"C major", "F# minor", "A minor", "D# major"})
        for (const char* loop : {"C major", "G minor", "B minor", "F major", "Am", "C#"}) {
            const int shift = semitonesToKey(loop, song);
            EXPECT_GE(shift, -6) << loop << " in " << song;
            EXPECT_LE(shift, 6) << loop << " in " << song;
        }
}

TEST(LoopStretchTest, SoundKeyForScaleNamesEveryScaleByItsThird) {
    EXPECT_EQ(soundKeyForScale(std::nullopt), "");
    const auto key = [](const char* name, int root) {
        for (const auto& preset : synth::builtInScalePresets())
            if (juce::String(preset.name) == name) {
                synth::MusicalScale scale;
                scale.rootPitchClass = root;
                scale.mask = preset.mask;
                scale.name = name;
                return soundKeyForScale(scale);
            }
        return juce::String("missing scale ") + name;
    };
    for (const char* name : {"Natural Minor", "Minor Pentatonic", "Harmonic Minor", "Melodic Minor", "Dorian",
                             "Phrygian", "Blues", "Locrian"})
        EXPECT_EQ(key(name, 2), "D minor") << name;
    for (const char* name : {"Major", "Major Pentatonic", "Lydian", "Mixolydian", "Phrygian Dominant", "Whole Tone"})
        EXPECT_EQ(key(name, 2), "D major") << name;
    EXPECT_EQ(key("Chromatic", 2), "") << "both thirds: no key";

    synth::MusicalScale noThird;
    noThird.rootPitchClass = 2;
    noThird.mask = 0x0081; // root and fifth only
    EXPECT_EQ(soundKeyForScale(noThird), "");
}
