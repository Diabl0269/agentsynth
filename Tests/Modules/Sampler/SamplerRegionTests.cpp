// SamplerRegionTests.cpp -- the Sampler's End point, Fine tune and Reverse.
#include "Modules/SamplerModule.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

juce::File writeWav(const juce::String& name, int numFrames, const std::function<float(int)>& generator) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
    file.deleteFile();
    juce::AudioBuffer<float> buffer(1, numFrames);
    for (int i = 0; i < numFrames; ++i)
        buffer.setSample(0, i, generator(i));

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return {};
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), kRate, 1, 32, {}, 0));
    if (writer == nullptr)
        return {};
    stream.release();
    writer->writeFromAudioSampleBuffer(buffer, 0, numFrames);
    writer.reset();
    return file;
}

// 0 -> 0.5 across the file: every frame distinct, and linear so a fractional read has a known value.
float ramp(double position, int numFrames) { return 0.5f * (float)(position / (double)(numFrames - 1)); }

// Distinct, non-monotonic values (each exact in a 32-bit WAV to ~5e-10): reversal cannot pass for
// anything else.
float scrambled(int index) { return 0.05f + 0.4f * (float)((index * 37) % 101) / 101.0f; }

float parameterValue(SamplerModule& module, const juce::String& id) {
    if (auto* f = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&module, id)))
        return f->get();
    if (auto* b = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&module, id)))
        return b->get() ? 1.0f : 0.0f;
    return -999.0f;
}

void setActual(SamplerModule& module, const juce::String& id, float value) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

// Left channel of `numBlocks` blocks, optionally holding `cv` on `cvChannel`.
std::vector<float> render(SamplerModule& module, int numBlocks, int cvChannel = -1, float cv = 0.0f) {
    std::vector<float> out;
    for (int b = 0; b < numBlocks; ++b) {
        juce::AudioBuffer<float> block(SamplerModule::kNumChannels, kBlock);
        block.clear();
        if (cvChannel >= 0)
            for (int i = 0; i < kBlock; ++i)
                block.setSample(cvChannel, i, cv);
        juce::MidiBuffer midi;
        module.processBlock(block, midi);
        out.insert(out.end(), block.getReadPointer(0), block.getReadPointer(0) + kBlock);
    }
    return out;
}

struct Rig {
    explicit Rig(const juce::String& name, int frames, const std::function<float(int)>& generator) {
        module.prepareToPlay(kRate, kBlock);
        file = writeWav(name, frames, generator);
        loaded = module.loadSampleFile(file);
        if (loaded)
            setActual(module, "level", 1.0f);
    }
    ~Rig() { file.deleteFile(); }

    SamplerModule module;
    juce::File file;
    bool loaded = false;
};

constexpr int kFade = SamplerModule::kFadeSamples;

} // namespace

TEST(SamplerRegion, ParametersJacksAndNeutralDefaults) {
    SamplerModule module;
    EXPECT_FLOAT_EQ(parameterValue(module, "end"), 1.0f);
    EXPECT_FLOAT_EQ(parameterValue(module, "fine"), 0.0f);
    EXPECT_FLOAT_EQ(parameterValue(module, "reverse"), 0.0f);
    auto* fine = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, "fine"));
    ASSERT_NE(fine, nullptr);
    EXPECT_FLOAT_EQ(fine->convertFrom0to1(0.0f), -100.0f);
    EXPECT_FLOAT_EQ(fine->convertFrom0to1(1.0f), 100.0f);
    ASSERT_NE(dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&module, "reverse")), nullptr);

    EXPECT_EQ(SamplerModule::kEndCVCh, 8);
    EXPECT_EQ(SamplerModule::kFineCVCh, 9);
    EXPECT_EQ(module.getInputPortLabel(SamplerModule::kEndCVCh), "End");
    EXPECT_EQ(module.getInputPortLabel(SamplerModule::kFineCVCh), "Fine");
    EXPECT_EQ(module.mapInputChannel(SamplerModule::kEndCVCh).role, PortRole::ModCV);
    EXPECT_EQ(module.mapInputChannel(SamplerModule::kFineCVCh).role, PortRole::ModCV);

    const auto targets = module.getModulationTargets();
    ASSERT_GE(targets.size(), 2u);
    EXPECT_EQ(targets[targets.size() - 2].paramId, "end");
    EXPECT_EQ(targets[targets.size() - 1].paramId, "fine");
    // The reverse switch is a bool, and none of the module's other bools has a jack.
    for (const auto& t : targets)
        EXPECT_NE(t.paramId, "reverse");
}

TEST(SamplerRegion, StateSavedBeforeTheNewControlsLoadsTheDefaultsAndPlaysAsBefore) {
    juce::ValueTree state("ModuleState"); // loop and level only, as a patch from before End/Fine/Reverse
    state.setProperty("loop", 0.0f, nullptr);
    state.setProperty("level", 1.0f, nullptr);
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), blob);

    constexpr int kFrames = 4096;
    Rig rig("sampler-region-legacy.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    ASSERT_TRUE(rig.loaded);
    rig.module.setStateInformation(blob.getData(), (int)blob.getSize());
    EXPECT_FLOAT_EQ(parameterValue(rig.module, "end"), 1.0f);
    EXPECT_FLOAT_EQ(parameterValue(rig.module, "fine"), 0.0f);
    EXPECT_FLOAT_EQ(parameterValue(rig.module, "reverse"), 0.0f);

    // One shot, forward, unity rate, to the very end of the file and then silence.
    const auto out = render(rig.module, 10);
    for (int i = kFade; i < kFrames - 2; ++i)
        ASSERT_NEAR(out[(size_t)i], ramp(i, kFrames), 1.0e-4f) << "frame " << i;
    for (int i = kFrames + kFade + 8; i < (int)out.size(); ++i)
        ASSERT_EQ(out[(size_t)i], 0.0f) << "frame " << i;
}

TEST(SamplerRegion, EndAtHalfStopsAtHalf) {
    constexpr int kFrames = 4096;
    Rig rig("sampler-region-end.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    ASSERT_TRUE(rig.loaded);
    setActual(rig.module, "loop", 0.0f);
    setActual(rig.module, "end", 0.5f);

    const auto out = render(rig.module, 10);
    const double endFrame = 0.5 * (kFrames - 1);
    for (int i = kFade; i < (int)endFrame; ++i)
        ASSERT_NEAR(out[(size_t)i], ramp(i, kFrames), 1.0e-4f) << "frame " << i;
    // Nothing from the second half of the file: past the end the output only ramps out and goes quiet.
    float quietPeak = 0.0f;
    for (int i = (int)endFrame + kFade + 2; i < (int)out.size(); ++i)
        quietPeak = std::max(quietPeak, std::abs(out[(size_t)i]));
    EXPECT_EQ(quietPeak, 0.0f);
    EXPECT_LE(*std::max_element(out.begin(), out.end()), ramp(endFrame, kFrames) + 1.0e-4f);
}

TEST(SamplerRegion, EndWithLoopOnWrapsBackToStart) {
    constexpr int kFrames = 4096;
    Rig rig("sampler-region-endloop.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    ASSERT_TRUE(rig.loaded);
    setActual(rig.module, "end", 0.5f);
    setActual(rig.module, "start", 0.25f);

    const auto out = render(rig.module, 12);
    const double startFrame = 0.25 * (kFrames - 1);
    const double endFrame = 0.5 * (kFrames - 1);
    const int period = (int)std::ceil(endFrame - startFrame);
    // Second pass through the region starts again at `start`, not at 0.
    for (int i = period + 2; i < period + 200; ++i) {
        const double position = startFrame + (double)(i - period);
        ASSERT_NEAR(out[(size_t)i], ramp(position, kFrames), 2.0e-3f) << "frame " << i;
    }
    EXPECT_LE(*std::max_element(out.begin(), out.end()), ramp(endFrame, kFrames) + 1.0e-4f);
}

TEST(SamplerRegion, EndCvMovesTheEndPoint) {
    constexpr int kFrames = 4096;
    Rig rig("sampler-region-endcv.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    ASSERT_TRUE(rig.loaded);
    setActual(rig.module, "loop", 0.0f);

    const auto out = render(rig.module, 10, SamplerModule::kEndCVCh, -0.5f); // 1.0 - 0.5
    EXPECT_LE(*std::max_element(out.begin(), out.end()), ramp(0.5 * (kFrames - 1), kFrames) + 1.0e-4f);
    EXPECT_GT(*std::max_element(out.begin(), out.end()), 0.2f);
}

TEST(SamplerRegion, StartAtOrPastEndIsAnEmptyRegionAndStaysSilent) {
    constexpr int kFrames = 4096;
    for (const int granular : {0, 1}) {
        SCOPED_TRACE(granular);
        Rig rig("sampler-region-empty.wav", kFrames, [](int i) { return ramp(i, kFrames); });
        ASSERT_TRUE(rig.loaded);
        setActual(rig.module, "playMode", (float)granular);
        setActual(rig.module, "start", 0.6f);
        setActual(rig.module, "end", 0.4f);
        for (const float v : render(rig.module, 6))
            ASSERT_EQ(v, 0.0f);

        // Start equal to end is empty as well, and so is an end of zero.
        setActual(rig.module, "start", 0.4f);
        for (const float v : render(rig.module, 3))
            ASSERT_EQ(v, 0.0f);
        setActual(rig.module, "start", 0.0f);
        setActual(rig.module, "end", 0.0f);
        for (const float v : render(rig.module, 3))
            ASSERT_EQ(v, 0.0f);
    }
}

TEST(SamplerRegion, TheRegionPlaysAgainOnceEndMovesBackOutPastStart) {
    constexpr int kFrames = 4096;
    Rig rig("sampler-region-recover.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    ASSERT_TRUE(rig.loaded);
    setActual(rig.module, "start", 0.5f);
    setActual(rig.module, "end", 0.25f);
    for (const float v : render(rig.module, 2))
        ASSERT_EQ(v, 0.0f);
    setActual(rig.module, "end", 1.0f);
    const auto out = render(rig.module, 4);
    EXPECT_GT(*std::max_element(out.begin(), out.end()), 0.2f);
}

TEST(SamplerRegion, HundredCentsOfFineIsOneSemitoneOfPitch) {
    constexpr int kFrames = 16384;
    Rig fine("sampler-region-fine.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    Rig semitone("sampler-region-fine2.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    ASSERT_TRUE(fine.loaded);
    ASSERT_TRUE(semitone.loaded);
    setActual(fine.module, "fine", 100.0f);
    setActual(semitone.module, "pitch", 1.0f);

    const auto a = render(fine.module, 2);
    const auto b = render(semitone.module, 2);
    for (size_t i = 0; i < a.size(); ++i)
        ASSERT_NEAR(a[i], b[i], 1.0e-6f) << "frame " << i;

    const double ratio = std::pow(2.0, 1.0 / 12.0);
    for (int i = kFade; i < 2 * kBlock; ++i)
        ASSERT_NEAR(a[(size_t)i], ramp((double)i * ratio, kFrames), 2.0e-4f) << "frame " << i;
}

TEST(SamplerRegion, FineAddsToPitchAndGoesBothWays) {
    constexpr int kFrames = 16384;
    Rig rig("sampler-region-fine3.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    ASSERT_TRUE(rig.loaded);
    setActual(rig.module, "pitch", 12.0f);
    setActual(rig.module, "fine", -50.0f);
    const auto out = render(rig.module, 1);
    const double ratio = std::pow(2.0, 11.5 / 12.0);
    for (int i = kFade; i < kBlock; ++i)
        ASSERT_NEAR(out[(size_t)i], ramp((double)i * ratio, kFrames), 2.0e-4f) << "frame " << i;
}

TEST(SamplerRegion, FineCvIsHundredCentsPerUnit) {
    constexpr int kFrames = 16384;
    Rig viaCv("sampler-region-finecv.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    Rig viaKnob("sampler-region-finecv2.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    ASSERT_TRUE(viaCv.loaded);
    ASSERT_TRUE(viaKnob.loaded);
    setActual(viaKnob.module, "fine", 100.0f);
    const auto a = render(viaCv.module, 2, SamplerModule::kFineCVCh, 1.0f);
    const auto b = render(viaKnob.module, 2);
    for (size_t i = 0; i < a.size(); ++i)
        ASSERT_NEAR(a[i], b[i], 1.0e-5f) << "frame " << i;
}

TEST(SamplerRegion, ReverseOutputIsTheTimeReversedRegion) {
    constexpr int kFrames = 1001; // end 0.5 -> frame 500, start 0.25 -> frame 250: both exact
    Rig rig("sampler-region-reverse.wav", kFrames, scrambled);
    ASSERT_TRUE(rig.loaded);
    setActual(rig.module, "loop", 0.0f);
    setActual(rig.module, "start", 0.25f);
    setActual(rig.module, "end", 0.5f);
    setActual(rig.module, "reverse", 1.0f);

    const auto out = render(rig.module, 4);
    // Frames 500 down to 250 are the closed region; the ramp-out starts on the last of them.
    for (int k = kFade; k < 250; ++k)
        ASSERT_NEAR(out[(size_t)k], scrambled(500 - k), 1.0e-5f) << "output frame " << k;
    // Played once: after the region and the ramp-out nothing is left.
    for (int k = 250 + kFade + 4; k < (int)out.size(); ++k)
        ASSERT_EQ(out[(size_t)k], 0.0f) << "output frame " << k;
}

TEST(SamplerRegion, ReverseWithLoopLoopsBackwards) {
    constexpr int kFrames = 1001;
    Rig rig("sampler-region-reverseloop.wav", kFrames, scrambled);
    ASSERT_TRUE(rig.loaded);
    setActual(rig.module, "start", 0.25f);
    setActual(rig.module, "end", 0.5f);
    setActual(rig.module, "reverse", 1.0f);

    const auto out = render(rig.module, 3);
    constexpr int kPeriod = 251; // frames 500 down to 250, then round again from 500
    for (int k = kFade; k < 3 * kPeriod; ++k)
        ASSERT_NEAR(out[(size_t)k], scrambled(500 - (k % kPeriod)), 1.0e-5f) << "output frame " << k;
}

TEST(SamplerRegion, ReverseWithTheDefaultRegionStartsAtTheLastFrame) {
    constexpr int kFrames = 2048;
    Rig rig("sampler-region-reversefull.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    ASSERT_TRUE(rig.loaded);
    setActual(rig.module, "reverse", 1.0f);
    const auto out = render(rig.module, 2);
    for (int k = kFade; k < kBlock; ++k)
        ASSERT_NEAR(out[(size_t)k], ramp(kFrames - 1 - k, kFrames), 1.0e-4f) << "output frame " << k;
}

TEST(SamplerRegion, GranularEndKeepsEveryGrainInsideTheRegion) {
    constexpr int kFrames = 8192;
    // Positive up to frame 4100, negative after: the end point sits at 4095.5.
    auto source = [](int i) { return i < 4100 ? 0.3f : -0.3f; };
    Rig bounded("sampler-region-grain-end.wav", kFrames, source);
    Rig open("sampler-region-grain-open.wav", kFrames, source);
    ASSERT_TRUE(bounded.loaded);
    ASSERT_TRUE(open.loaded);
    for (auto* module : {&bounded.module, &open.module}) {
        setActual(*module, "playMode", 1.0f);
        setActual(*module, "spray", 1.0f);
        setActual(*module, "density", 60.0f);
        setActual(*module, "grainSize", 120.0f);
    }
    setActual(bounded.module, "end", 0.5f);

    const auto limited = render(bounded.module, 80);
    const auto unlimited = render(open.module, 80);
    EXPECT_GE(*std::min_element(limited.begin(), limited.end()), -1.0e-6f) << "no grain may read past End";
    EXPECT_GT(*std::max_element(limited.begin(), limited.end()), 0.01f);
    EXPECT_LT(*std::min_element(unlimited.begin(), unlimited.end()), -0.01f)
        << "control: without End the same cloud does reach the negative half";
}

TEST(SamplerRegion, GranularReversePlaysEachGrainBackwards) {
    constexpr int kFrames = 8193; // start 0.5 -> frame 4096 exactly
    Rig forward("sampler-region-grain-fwd.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    Rig backward("sampler-region-grain-rev.wav", kFrames, [](int i) { return ramp(i, kFrames); });
    ASSERT_TRUE(forward.loaded);
    ASSERT_TRUE(backward.loaded);
    for (auto* module : {&forward.module, &backward.module}) {
        setActual(*module, "playMode", 1.0f);
        setActual(*module, "start", 0.5f);
        setActual(*module, "spray", 0.0f);
        setActual(*module, "density", 1.0f);
        setActual(*module, "grainSize", 100.0f);
    }
    setActual(backward.module, "reverse", 1.0f);

    const auto fwd = render(forward.module, 4);
    const auto rev = render(backward.module, 4);
    // The first grain spawns on the first sample: out[k] = hann(k) * ramp(4096 +/- k).
    const int length = (int)(0.1 * kRate);
    for (int k = kFade; k < 1500; ++k) {
        const float window = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float)k / (float)length);
        ASSERT_NEAR(fwd[(size_t)k], window * ramp(4096.0 + k, kFrames), 1.0e-4f) << "forward frame " << k;
        ASSERT_NEAR(rev[(size_t)k], window * ramp(4096.0 - k, kFrames), 1.0e-4f) << "reverse frame " << k;
    }
}
