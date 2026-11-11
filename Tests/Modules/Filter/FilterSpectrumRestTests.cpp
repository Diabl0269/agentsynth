// FilterSpectrumRestTests.cpp -- a muted or bypassed Filter feeds its scope and spectrum buffers with what leaves it,
// so the spectrum falls away instead of re-analysing the last audible block forever.
#include "Modules/FilterModule.h"
#include "UI/ModuleViews/FilterSpectrumAnalyser.h"
#include <cmath>
#include <gtest/gtest.h>

using synth::ui::FilterSpectrumAnalyser;

namespace {

constexpr double kSpectrumRestRate = 44100.0;
constexpr int kSpectrumRestBlock = 512;
double gSpectrumRestNow = 0.0;
double spectrumRestClock() { return gSpectrumRestNow += 1.0 / 30.0; }

void processTone(FilterModule& f, int blocks, bool silent, long& phase) {
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> buffer(f.getTotalNumInputChannels(), kSpectrumRestBlock);
        buffer.clear();
        if (!silent)
            for (int i = 0; i < kSpectrumRestBlock; ++i, ++phase)
                buffer.setSample(0, i,
                                 (float)(0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * (double)phase /
                                                        kSpectrumRestRate)));
        f.processBlock(buffer, midi);
    }
}

/** Runs the spectrum over `f`'s spectrum buffer for 2 s of simulated time at 30 Hz; true when every point is at kMinDb.
 */
bool spectrumFallsToFloor(FilterModule& f) {
    FilterSpectrumAnalyser analyser;
    gSpectrumRestNow = 0.0;
    FilterSpectrumAnalyser::setClockForTest(&spectrumRestClock);
    for (int i = 0; i < 60; ++i)
        analyser.analyse(f.getSpectrumBuffer(), kSpectrumRestRate);
    FilterSpectrumAnalyser::setClockForTest(nullptr);
    for (float db : analyser.getDb())
        if (db > FilterSpectrumAnalyser::kMinDb + 0.01f)
            return false;
    return true;
}

/** A Filter that has played a 1 kHz tone with its spectrum showing it, wide open so the tone passes. */
void playTone(FilterModule& f, long& phase) {
    f.prepareToPlay(kSpectrumRestRate, kSpectrumRestBlock);
    for (auto* p : f.getParameters())
        if (p->getName(100) == "Cutoff")
            p->setValueNotifyingHost(1.0f);
    processTone(f, 20, false, phase);

    FilterSpectrumAnalyser analyser;
    gSpectrumRestNow = 0.0;
    FilterSpectrumAnalyser::setClockForTest(&spectrumRestClock);
    analyser.analyse(f.getSpectrumBuffer(), kSpectrumRestRate);
    FilterSpectrumAnalyser::setClockForTest(nullptr);
    float peak = FilterSpectrumAnalyser::kMinDb;
    for (float db : analyser.getDb())
        peak = std::max(peak, db);
    ASSERT_GT(peak, -30.0f) << "the tone must show before the filter is silenced";
}

} // namespace

// Regression test for FRO1590: a muted Filter returned before pushing to its buffers, so the spectrum kept the last
// audible block forever.
TEST(FilterSpectrumRest, MutedFilterSpectrumFallsToTheFloor) {
    FilterModule f;
    long phase = 0;
    playTone(f, phase);
    f.setMuted(true);
    processTone(f, 40, false, phase);
    EXPECT_TRUE(spectrumFallsToFloor(f));
}

// Regression test for FRO1590: the same for a bypassed Filter, which passes its (here silent) input through.
TEST(FilterSpectrumRest, BypassedFilterSpectrumShowsTheSilentDrySignal) {
    FilterModule f;
    long phase = 0;
    playTone(f, phase);
    f.setBypassed(true);
    processTone(f, 40, true, phase);
    EXPECT_TRUE(spectrumFallsToFloor(f));
}
