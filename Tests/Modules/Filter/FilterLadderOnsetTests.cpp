// The first note a ladder plays starts at the cutoff it was set to: the ladder's cutoff and resonance smoothers only
// advance while it processes, so a ladder that has never run (first block, a poly voice with no note yet) or sat idle
// while the knob moved used to glide for 50 ms from its old cutoff, which dulled and softened the first few periods.
// Measured on a 110 Hz saw (peak 0.2) through LPF12 at 3 kHz, resonance 0.2, Drive 1.
#include "CustomFilterDriveHelpers.h"
#include "Modules/FilterModule.h"
#include <algorithm>
#include <gtest/gtest.h>

using namespace synth::test::drive;

namespace {

constexpr double kHz = 110.0;
constexpr float kLpf12 = 1.0f;
constexpr int kPeriod = 436; // samples per period at 48 kHz, to the nearest sample
constexpr int kNoteSamples = 20 * kBlock;

void configure(FilterModule& m, bool poly) {
    setParam(m, "filterType", kLpf12);
    setParam(m, "cutoff", 3000.0f);
    setParam(m, "resonance", 0.2f);
    setParam(m, "drive", 1.0f);
    setParam(m, "poly", poly ? 1.0f : 0.0f);
}

/** A note after `silentBlocks` silent blocks at `idleCutoff` and as many back at 3 kHz, so the ladder holds a stale
    cutoff when the saw arrives (0 blocks: the saw is the first thing the module ever processes). */
std::vector<float> note(bool poly, int silentBlocks, float idleCutoff) {
    FilterModule m;
    configure(m, poly);
    m.prepareToPlay(kSampleRate, kBlock);
    juce::AudioBuffer<float> buf(kChannels, kBlock);
    juce::MidiBuffer midi;
    auto silent = [&] {
        buf.clear();
        juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), kChannels, kBlock);
        m.processBlock(view, midi);
    };
    if (silentBlocks > 0) {
        setParam(m, "cutoff", idleCutoff);
        for (int b = 0; b < silentBlocks; ++b)
            silent();
        setParam(m, "cutoff", 3000.0f);
        for (int b = 0; b < silentBlocks; ++b)
            silent();
    }
    const auto in = sawTone(kHz, 0.2f, kNoteSamples);
    std::vector<float> out(in.size());
    for (size_t start = 0; start < in.size(); start += kBlock) {
        buf.clear();
        std::copy_n(in.data() + start, kBlock, buf.getWritePointer(0));
        juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), kChannels, kBlock);
        m.processBlock(view, midi);
        std::copy_n(buf.getReadPointer(0), kBlock, out.data() + start);
    }
    return out;
}

double peakToPeak(const std::vector<float>& x, int from, int n) {
    const auto [lo, hi] = std::minmax_element(x.begin() + from, x.begin() + from + n);
    return (double)*hi - (double)*lo;
}

/** Peak over rms of `n` samples from `from`. */
double crest(const std::vector<float>& x, int from, int n) {
    double s = 0.0, pk = 0.0;
    for (int i = from; i < from + n; ++i) {
        s += (double)x[(size_t)i] * x[(size_t)i];
        pk = std::max(pk, std::abs((double)x[(size_t)i]));
    }
    return pk / std::sqrt(s / n);
}

/** Before the fix (first note out of a never-run ladder, 3 kHz): the first period was 0.79 of a steady one, the first
    20 ms had a crest of 1.96 against 1.74 steady, and nothing above 2 kHz. With the ladder settled: 1.00 and 1.81
    (the saw's own start). The tolerances sit between the two. */
void expectSharpStart(const std::vector<float>& x) {
    const double firstOverSteady = peakToPeak(x, 0, kPeriod) / peakToPeak(x, 20 * kPeriod, kPeriod);
    EXPECT_GE(firstOverSteady, 0.97) << "the first period is already at the level of a steady one";
    EXPECT_LE(crest(x, 0, 960), crest(x, 20 * kPeriod, 5 * kPeriod) * 1.08)
        << "the first 20 ms has the crest factor of the steady note";
}

} // namespace

TEST(FilterLadderOnset, FirstNoteOutOfAFreshMonoFilterStartsAtFullLevel) { expectSharpStart(note(false, 0, 0.0f)); }

TEST(FilterLadderOnset, FirstNoteOutOfAFreshPolyVoiceStartsAtFullLevel) { expectSharpStart(note(true, 0, 0.0f)); }

TEST(FilterLadderOnset, NoteAfterTheCutoffMovedWhileTheVoiceWasIdleStartsAtFullLevel) {
    expectSharpStart(note(true, 8, 400.0f));
}
