// FilterSweepClickTests.cpp
//
// Sweeping the Filter's Cutoff and Resonance under a held note must not tick. Each swept render is checked with the
// shared click detector and set against the same note rendered with the knobs held still: a tick the held render
// does not have is a step in the filter, not in the note.

#include "../Engine/AudioRendering/ClickDetector.h"
#include "Modules/FilterModule.h"
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr double kSweepNote = 130.8128; // C3
constexpr int kSweepBlock = 64;         // a knob step every 64 samples, rougher than any UI or automation lane

void setSweepKnob(FilterModule& filter, const char* id, float value) {
    for (auto* p : filter.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p); ranged != nullptr && ranged->paramID == id)
            ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
}

// The held note: C3 with its 3rd and 20th partials, all smooth sines, so a click in the output can only come from
// the filter (a saw's own reset edge reads as a click once the filter lets it through).
std::vector<float> heldNote(double sampleRate, int numSamples) {
    std::vector<float> out((size_t)numSamples, 0.0f);
    const std::pair<int, double> partials[] = {{1, 0.4}, {3, 0.2}, {20, 0.1}};
    for (const auto& [n, gain] : partials) {
        const double step = 2.0 * juce::MathConstants<double>::pi * n * kSweepNote / sampleRate;
        for (int i = 0; i < numSamples; ++i)
            out[(size_t)i] += (float)(gain * std::sin(step * i));
    }
    return out;
}

struct SweepShape {
    float cutoffLow, cutoffHigh; // Hz, swept up and back down on a log scale
    float resLow, resHigh;       // swept up and back down
    bool sweepCutoff, sweepRes;
};

// Triangle 0 -> 1 -> 0 across the render.
float triangle(double t) { return (float)(t < 0.5 ? 2.0 * t : 2.0 * (1.0 - t)); }

std::vector<float> renderSweep(double sampleRate, const SweepShape& shape, int type) {
    auto filter = std::make_unique<FilterModule>(); // heap: the 16 ladders are too big for a Windows test stack
    setSweepKnob(*filter, "filterType", (float)type);
    setSweepKnob(*filter, "cutoff", shape.cutoffLow);
    setSweepKnob(*filter, "resonance", shape.resLow);
    filter->prepareToPlay(sampleRate, kSweepBlock);
    const int total = (int)(sampleRate * 4.0) / kSweepBlock * kSweepBlock;
    const auto note = heldNote(sampleRate, total);
    std::vector<float> out((size_t)total);
    juce::MidiBuffer midi;
    for (int pos = 0; pos + kSweepBlock <= total; pos += kSweepBlock) {
        const double t = (double)pos / total;
        if (shape.sweepCutoff)
            setSweepKnob(*filter, "cutoff",
                         shape.cutoffLow * std::pow(shape.cutoffHigh / shape.cutoffLow, triangle(t)));
        if (shape.sweepRes)
            setSweepKnob(*filter, "resonance", shape.resLow + (shape.resHigh - shape.resLow) * triangle(t));
        juce::AudioBuffer<float> block(FilterModule::kNumChannels, kSweepBlock);
        block.clear();
        std::copy(note.begin() + pos, note.begin() + pos + kSweepBlock, block.getWritePointer(0));
        filter->processBlock(block, midi);
        std::copy(block.getReadPointer(0), block.getReadPointer(0) + kSweepBlock, out.begin() + pos);
    }
    return out;
}

// Clicks after the first half second (the note's own start), as indices.
std::vector<clickdetector::Click> clicksAfterStart(const std::vector<float>& x, double sampleRate) {
    std::vector<float> body(x.begin() + (long)(sampleRate * 0.5), x.end());
    return clickdetector::findClicks(body);
}

} // namespace

// Regression test for FRO1348: the new ladder (headroom, feedback ceiling, no feedback floor) must glide through
// every Cutoff and Resonance move; a held-knob render of the same note is the baseline.
TEST(FilterSweepClick, SweepingCutoffAndResonanceAddsNoClicksOverHeldKnobs) {
    const SweepShape shapes[] = {
        {200.0f, 18000.0f, 1.0f, 1.0f, true, false}, // cutoff up and down at full resonance, through the ceiling
        {200.0f, 18000.0f, 0.9f, 0.9f, true, false}, // the same at 90 %
        {8000.0f, 8000.0f, 0.0f, 1.0f, false, true}, // resonance up and down at a cutoff with a low ceiling
        {1000.0f, 1000.0f, 0.0f, 1.0f, false, true}, // and at 1 kHz
        {300.0f, 16000.0f, 0.0f, 1.0f, true, true},  // both at once
    };
    for (double sr : {44100.0, 48000.0})
        for (int type : {0, 1, 2, 3, 4, 5})
            for (const auto& shape : shapes) {
                const auto swept = renderSweep(sr, shape, type);
                for (float v : swept)
                    ASSERT_TRUE(std::isfinite(v)) << "sr " << sr << " type " << type;
                // Held knobs at the sweep's highest-stress setting: the top of the cutoff range and the resonance peak.
                SweepShape held = shape;
                held.sweepCutoff = held.sweepRes = false;
                held.cutoffLow = shape.cutoffHigh;
                held.resLow = shape.resHigh;
                const auto still = renderSweep(sr, held, type);
                const auto sweptClicks = clicksAfterStart(swept, sr);
                const auto stillClicks = clicksAfterStart(still, sr);
                EXPECT_LE(sweptClicks.size(), stillClicks.size())
                    << "sr " << sr << " type " << type << " cutoff " << shape.cutoffLow << "-" << shape.cutoffHigh
                    << " res " << shape.resLow << "-" << shape.resHigh << ": first click at sample "
                    << (sweptClicks.empty() ? -1 : sweptClicks[0].index) << " ratio "
                    << (sweptClicks.empty() ? 0.0f : sweptClicks[0].ratio);
            }
}
