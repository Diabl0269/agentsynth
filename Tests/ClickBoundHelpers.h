#pragma once

// The click bound the module unit tests share with Tests/Engine/AudioRendering/ModuleClickAuditTests.cpp:
// a module is fed a steady 440 Hz sine, one edge happens between two blocks, and the largest
// sample-to-sample step after the edge must stay within a few times the largest step the output shows
// while steady (before the edge and once it has settled). Header-only; not compiled on its own.

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <utility>
#include <vector>

namespace clickbound {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kSettleBlocks = 12; // blocks before the edge (the last kReferenceBlocks are the steady reference)
constexpr int kReferenceBlocks = 4;
constexpr int kEdgeBlocks = 40; // blocks after the edge
constexpr int kTailBlocks = 8;  // last blocks, taken as the settled output
constexpr float kTolerance = 4.0f;
constexpr float kFloor = 1.0e-4f;

struct Input {
    int audioChannels = 2;                        // leading channels fed the sine
    std::vector<std::pair<int, float>> held = {}; // channels held at a constant (gates, steady CV)
    float amplitude = 0.5f;
    float hz = 440.0f;
};

struct Steps {
    float steady = 0.0f; // largest step of the reference and settled blocks
    float edge = 0.0f;   // largest step from the edge block on
    float limit() const { return kTolerance * std::max(steady, kFloor); }
};

inline void setActual(juce::AudioProcessor& m, const juce::String& id, float value) {
    for (auto* p : m.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id) {
            r->setValueNotifyingHost(r->convertTo0to1(value));
            return;
        }
    FAIL() << "no parameter " << id;
}

inline void setNormalised(juce::AudioProcessor& m, const juce::String& id, float normalised) {
    for (auto* p : m.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id) {
            r->setValueNotifyingHost(normalised);
            return;
        }
    FAIL() << "no parameter " << id;
}

/** Renders the settle + edge blocks of `m` (already prepared) and returns every block's output, one vector
    per `watch` channel. `onBlock(b)` runs before block b; block kSettleBlocks is the edge. */
template <typename OnBlock>
std::vector<std::vector<float>> render(juce::AudioProcessor& m, const Input& in, const std::vector<int>& watch,
                                       OnBlock onBlock) {
    const int channels = std::max(m.getTotalNumInputChannels(), m.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer(channels, kBlock);
    std::vector<std::vector<float>> out(watch.size());
    double phase = 0.0;
    const double step = 2.0 * juce::MathConstants<double>::pi * in.hz / kRate;
    for (int b = 0; b < kSettleBlocks + kEdgeBlocks; ++b) {
        onBlock(b);
        buffer.clear();
        for (int ch = 0; ch < in.audioChannels && ch < channels; ++ch)
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample(ch, i, in.amplitude * (float)std::sin(phase + step * i));
        phase += step * kBlock;
        for (const auto& [ch, value] : in.held)
            if (ch < channels)
                juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), value, kBlock);
        juce::MidiBuffer midi;
        m.processBlock(buffer, midi);
        for (size_t w = 0; w < watch.size(); ++w)
            out[w].insert(out[w].end(), buffer.getReadPointer(watch[w]), buffer.getReadPointer(watch[w]) + kBlock);
    }
    return out;
}

inline float largestStep(const std::vector<float>& x, int fromBlock, int toBlock) {
    float worst = 0.0f;
    for (int i = std::max(1, fromBlock * kBlock); i < toBlock * kBlock; ++i)
        worst = std::max(worst, std::abs(x[(size_t)i] - x[(size_t)i - 1]));
    return worst;
}

inline Steps measure(const std::vector<std::vector<float>>& channels) {
    Steps s;
    for (const auto& x : channels) {
        s.steady = std::max({s.steady, largestStep(x, kSettleBlocks - kReferenceBlocks, kSettleBlocks),
                             largestStep(x, kSettleBlocks + kEdgeBlocks - kTailBlocks, kSettleBlocks + kEdgeBlocks)});
        s.edge = std::max(s.edge, largestStep(x, kSettleBlocks, kSettleBlocks + kEdgeBlocks));
    }
    return s;
}

/** Prepares `m`, renders with `edge()` applied between the settle and edge blocks, and returns the steps of
    the `watch` channels. */
template <typename Edge>
Steps measureEdge(juce::AudioProcessor& m, const Input& in, const std::vector<int>& watch, Edge edge) {
    m.setPlayConfigDetails(m.getTotalNumInputChannels(), m.getTotalNumOutputChannels(), kRate, kBlock);
    m.prepareToPlay(kRate, kBlock);
    return measure(render(m, in, watch, [&](int b) {
        if (b == kSettleBlocks)
            edge();
    }));
}

#define EXPECT_NO_CLICK(steps)                                                                                         \
    EXPECT_LE((steps).edge, (steps).limit())                                                                           \
        << "largest step " << (steps).edge << " against a steady step of " << (steps).steady

} // namespace clickbound
