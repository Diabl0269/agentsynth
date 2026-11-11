// VisualBufferOnMuteTests.cpp -- the guard for every module with a scope: muted, its visual buffer shows silence, not
// the last audible block. A module that returns early on mute must still feed the buffer.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"
#include "Modules/VisualBuffer.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <map>

namespace {

constexpr double kVisualBufferRate = 44100.0;
constexpr int kVisualBufferBlock = 512;

/** Module types whose buffer is legitimately not silent when muted; each with the reason. */
const std::map<juce::String, const char*>& exceptions() {
    static const std::map<juce::String, const char*> e = {};
    return e;
}

void run(ModuleBase& m, int blocks, long& phase) {
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b) {
        juce::AudioBuffer<float> buffer(std::max(m.getTotalNumInputChannels(), m.getTotalNumOutputChannels()),
                                        kVisualBufferBlock);
        buffer.clear();
        for (int i = 0; i < kVisualBufferBlock; ++i, ++phase)
            buffer.setSample(0, i,
                             (float)(0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * (double)phase /
                                                    kVisualBufferRate)));
        m.processBlock(buffer, midi);
    }
}

float peakOf(const VisualBuffer& vb) {
    std::vector<float> data((size_t)vb.getSize());
    vb.copyTo(data);
    float peak = 0.0f;
    for (float v : data)
        peak = std::max(peak, std::abs(v));
    return peak;
}

} // namespace

TEST(VisualBufferOnMute, EveryMutedModuleShowsSilence) {
    int checked = 0;
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        if (exceptions().count(type) != 0)
            continue;
        auto processor = synth::AIStateMapper::createModule(type);
        auto* m = dynamic_cast<ModuleBase*>(processor.get());
        if (m == nullptr || m->getVisualBuffer() == nullptr || !m->hasMuteParameter())
            continue;
        m->prepareToPlay(kVisualBufferRate, kVisualBufferBlock);
        long phase = 0;
        run(*m, 20, phase);
        m->setMuted(true);
        run(*m, 60, phase);
        EXPECT_LT(peakOf(*m->getVisualBuffer()), 1.0e-4f) << type << " keeps showing its last audible block when muted";
        ++checked;
    }
    EXPECT_GT(checked, 5);
}
