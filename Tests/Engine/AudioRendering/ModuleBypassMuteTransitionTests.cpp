// The click-free bypass/mute transition ModuleBase adds around every module's own two-branch
// bypass/mute code (Source/Modules/ModuleOutputStage.cpp). A Distortion stands in for the FX set;
// the full module-by-module sweep is ModuleClickAuditTests.cpp.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioRenderingTestHelpers.h"
#include "HostedSineSource.h"
#include <vector>

namespace {

constexpr int kSettle = 8;
constexpr int kAfter = 6;
constexpr float kAmp = 0.5f;

struct Run {
    std::vector<juce::AudioBuffer<float>> in;
    std::vector<juce::AudioBuffer<float>> out;
};

/** Feeds a 440 Hz sine on both legs in `blockSize` blocks; `onBlock(i)` runs before block i. */
template <typename Fn>
Run runDistortion(juce::AudioProcessor& m, int blocks, int blockSize, Fn onBlock, int prepareBlock = 0) {
    const int prepared = prepareBlock > 0 ? prepareBlock : blockSize;
    m.setPlayConfigDetails(m.getTotalNumInputChannels(), m.getTotalNumOutputChannels(), kSampleRate, prepared);
    m.prepareToPlay(kSampleRate, prepared);
    const int channels = std::max(m.getTotalNumInputChannels(), m.getTotalNumOutputChannels());
    Run run;
    double phase = 0.0;
    const double step = 2.0 * juce::MathConstants<double>::pi * 440.0 / kSampleRate;
    for (int b = 0; b < blocks; ++b) {
        onBlock(b);
        juce::AudioBuffer<float> buf(channels, blockSize);
        buf.clear();
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buf.setSample(ch, i, kAmp * (float)std::sin(phase + step * i));
        phase += step * blockSize;
        run.in.push_back(buf);
        juce::MidiBuffer midi;
        m.processBlock(buf, midi);
        run.out.push_back(buf);
    }
    return run;
}

float worstStep(const Run& run, int fromBlock, int toBlock) {
    float worst = 0.0f;
    for (int ch = 0; ch < 2; ++ch) {
        bool have = false;
        float prev = 0.0f;
        for (int b = fromBlock; b < toBlock; ++b)
            for (int i = 0; i < run.out[(size_t)b].getNumSamples(); ++i) {
                const float v = run.out[(size_t)b].getSample(ch, i);
                if (have)
                    worst = std::max(worst, std::abs(v - prev));
                prev = v;
                have = true;
            }
    }
    return worst;
}

bool blocksEqual(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int channels) {
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (a.getSample(ch, i) != b.getSample(ch, i))
                return false;
    return true;
}

} // namespace

// Regression test for FRO1021: a bypass or mute toggle between two blocks was a one-block hard cut.
TEST(ModuleBypassMuteTransition, BypassToggleMidStreamStaysWithinTheAuditBound) {
    for (const bool turnOn : {true, false}) {
        DistortionModule m;
        if (!turnOn)
            m.setBypassed(true);
        auto run = runDistortion(m, kSettle + kAfter, kBlockSize, [&](int b) {
            if (b == kSettle)
                m.setBypassed(turnOn);
        });
        const float steady =
            std::max(worstStep(run, 4, kSettle), worstStep(run, kSettle + kAfter - 2, kSettle + kAfter));
        EXPECT_LE(worstStep(run, kSettle, kSettle + kAfter), 4.0f * steady) << "turnOn=" << turnOn;
    }
}

TEST(ModuleBypassMuteTransition, MuteToggleMidStreamStaysWithinTheAuditBound) {
    for (const bool turnOn : {true, false}) {
        DistortionModule m;
        if (!turnOn)
            m.setMuted(true);
        auto run = runDistortion(m, kSettle + kAfter, kBlockSize, [&](int b) {
            if (b == kSettle)
                m.setMuted(turnOn);
        });
        const float steady =
            std::max(worstStep(run, 4, kSettle), worstStep(run, kSettle + kAfter - 2, kSettle + kAfter));
        EXPECT_LE(worstStep(run, kSettle, kSettle + kAfter), 4.0f * steady) << "turnOn=" << turnOn;
    }
}

TEST(ModuleBypassMuteTransition, BypassedOutputIsTheDryInputFromTheBlockAfterTheToggle) {
    DistortionModule m;
    auto run = runDistortion(m, kSettle + kAfter, kBlockSize, [&](int b) {
        if (b == kSettle)
            m.setBypassed(true);
    });
    // The toggle block still renders the wet signal while it fades to dry.
    EXPECT_FALSE(blocksEqual(run.out[kSettle], run.in[kSettle], 2));
    for (int b = kSettle + 1; b < kSettle + kAfter; ++b)
        EXPECT_TRUE(blocksEqual(run.out[(size_t)b], run.in[(size_t)b], 2)) << "block " << b;
}

TEST(ModuleBypassMuteTransition, MutedOutputIsSilentFromTheBlockAfterTheToggle) {
    DistortionModule m;
    auto run = runDistortion(m, kSettle + kAfter, kBlockSize, [&](int b) {
        if (b == kSettle)
            m.setMuted(true);
    });
    EXPECT_GT(run.out[kSettle].getMagnitude(0, 0, kBlockSize), 0.0f);
    EXPECT_EQ(run.out[kSettle].getSample(0, kBlockSize - 1), 0.0f); // the fade has ended inside the block
    for (int b = kSettle + 1; b < kSettle + kAfter; ++b)
        EXPECT_EQ(run.out[(size_t)b].getMagnitude(0, 0, kBlockSize), 0.0f) << "block " << b;
}

TEST(ModuleBypassMuteTransition, TheToggleBlockStartsAtTheOldLevelAndEndsAtTheNew) {
    DistortionModule m;
    m.setMuted(true);
    auto run = runDistortion(m, kSettle + 2, kBlockSize, [&](int b) {
        if (b == kSettle)
            m.setMuted(false);
    });
    EXPECT_LT(std::abs(run.out[kSettle].getSample(0, 0)), 0.01f); // fades in from zero
    EXPECT_GT(run.out[kSettle].getMagnitude(0, kBlockSize / 2, kBlockSize / 2), 0.05f);
}

TEST(ModuleBypassMuteTransition, WithoutATransitionTheOutputIsBitIdenticalToTheModulesOwnRender) {
    DistortionModule viaBase, direct;
    for (auto* m : {&viaBase, &direct}) {
        m->setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
        m->prepareToPlay(kSampleRate, kBlockSize);
    }
    double phase = 0.0;
    for (int b = 0; b < 10; ++b) {
        juce::AudioBuffer<float> x(2, kBlockSize);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlockSize; ++i)
                x.setSample(ch, i, kAmp * (float)std::sin(phase + 0.06 * i));
        phase += 0.06 * kBlockSize;
        juce::AudioBuffer<float> y;
        y.makeCopyOf(x);
        juce::MidiBuffer midi;
        viaBase.processBlock(x, midi);
        direct.processModuleBlock(y, midi);
        EXPECT_TRUE(blocksEqual(x, y, 2)) << "block " << b;
    }
}

TEST(ModuleBypassMuteTransition, TheParameterReadsImmediatelyOffTheRenderThread) {
    DistortionModule m;
    m.setBypassed(true);
    EXPECT_TRUE(m.isBypassed());
    m.setMuted(true);
    EXPECT_TRUE(m.isMuted());
}

TEST(ModuleBypassMuteTransition, ARampLongerThanTheBlockContinuesIntoTheNextBlocks) {
    DistortionModule m;
    constexpr int kShort = 16; // 4 blocks per 64-sample minimum ramp
    auto run = runDistortion(m, 160, kShort, [&](int b) {
        if (b == 80)
            m.setBypassed(true);
    });
    const float steady = worstStep(run, 40, 80);
    EXPECT_LE(worstStep(run, 80, 100), 4.0f * steady);
    for (int b = 110; b < 160; ++b)
        EXPECT_TRUE(blocksEqual(run.out[(size_t)b], run.in[(size_t)b], 2)) << "block " << b;
}

TEST(ModuleBypassMuteTransition, ABlockLargerThanPreparedFallsBackToTheHardSwitch) {
    DistortionModule m;
    constexpr int kHuge = 8192; // past the capture storage prepareToPlay sized for 512
    auto run = runDistortion(
        m, 4, kHuge,
        [&](int b) {
            if (b == 2)
                m.setBypassed(true);
        },
        kBlockSize);
    EXPECT_TRUE(blocksEqual(run.out[2], run.in[2], 2)); // dry on the very block of the toggle, never a crash
    EXPECT_TRUE(blocksEqual(run.out[3], run.in[3], 2));
}

TEST(ModuleBypassMuteTransition, ALargeBlockStillCrossfadesWhenPreparedForIt) {
    DistortionModule m;
    constexpr int kHuge = 8192;
    auto run = runDistortion(m, 4, kHuge, [&](int b) {
        if (b == 2)
            m.setBypassed(true);
    });
    EXPECT_FALSE(blocksEqual(run.out[2], run.in[2], 2));
    EXPECT_TRUE(blocksEqual(run.out[3], run.in[3], 2));
}

TEST(ModuleBypassMuteTransition, ALoadedBypassedModuleStartsBypassedWithoutAFade) {
    DistortionModule m;
    m.setBypassed(true);
    auto run = runDistortion(m, 2, kBlockSize, [](int) {});
    EXPECT_TRUE(blocksEqual(run.out[0], run.in[0], 2));
}

// Regression test for FRO1080: muting a track whose source is a hosted plug-in cut the sound in one step. The
// host must fade a hosted instrument out and in like any other module, whatever the plug-in itself does.
TEST(ModuleBypassMuteTransition, MutingAHostedPluginSourceStaysWithinTheSignalsOwnSlope) {
    for (const bool turnOn : {true, false}) {
        synth::test::StubBackend backend(clickrig::sineFactory());
        auto module = clickrig::makeHostedSine(kSampleRate, kBlockSize, backend);
        ASSERT_TRUE(module->hasInstance());
        if (!turnOn)
            module->setMuted(true);
        const int channels = module->getTotalNumOutputChannels();
        std::vector<float> left, right;
        for (int b = 0; b < kSettle + kAfter; ++b) {
            if (b == kSettle)
                module->setMuted(turnOn);
            juce::AudioBuffer<float> buf(channels, kBlockSize);
            buf.clear();
            juce::MidiBuffer midi;
            module->processBlock(buf, midi);
            left.insert(left.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlockSize);
            right.insert(right.end(), buf.getReadPointer(1), buf.getReadPointer(1) + kBlockSize);
        }
        // The sine's own largest step is 0.5 * 2 pi 220 / fs; the fade may add a small share of that.
        const float ownSlope = 0.5f * 2.0f * juce::MathConstants<float>::pi * 220.0f / (float)kSampleRate;
        for (const auto* leg : {&left, &right}) {
            float worst = 0.0f;
            for (size_t i = (size_t)kSettle * kBlockSize - 1; i + 1 < leg->size(); ++i)
                worst = std::max(worst, std::abs((*leg)[i + 1] - (*leg)[i]));
            EXPECT_LE(worst, 2.0f * ownSlope)
                << "turnOn=" << turnOn << ": a step of " << worst << " against a slope of " << ownSlope;
        }
    }
}
