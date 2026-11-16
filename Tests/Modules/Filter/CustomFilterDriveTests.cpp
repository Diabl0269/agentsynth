// The Custom filter's Drive knob: it used to be shown and ignored. Now it saturates ahead of the drawn response.
// Drive 1 leaves the signal alone, the knob raises level and harmonic distortion along the curve measured from a
// commercial reference synth's low-pass drive, and the Drive CV moves it in mono and in poly.
#include "AllocationCounter.h"
#include "CustomFilterDriveHelpers.h"
#include "CustomFilterTestHelpers.h"
#include "Modules/Filter/CustomFilterDrive.h"
#include <gtest/gtest.h>

using namespace synth;
using namespace synth::test;
using namespace synth::test::drive;

namespace {

constexpr int kTotal = 96000;
constexpr int kWindow = 48000; // 1 s: a whole number of periods of every test tone
constexpr double kSineHz = 220.0;
constexpr float kKnob[5] = {1.0f, 3.25f, 5.5f, 7.75f, 10.0f};

/** Custom, drawn flat (0 dB everywhere), resonance 0: the filter itself is transparent, so what the output shows is
    the Drive stage. */
void configureFlatCustom(FilterModule& m, float driveKnob) {
    setParam(m, "filterType", (float)FilterModule::kCustomTypeIndex);
    setParam(m, "resonance", 0.0f);
    setParam(m, "drive", driveKnob);
    CustomFilterShapes s;
    s.shapes = {makeShape("flat", {{20.0f, 0.0f}, {20000.0f, 0.0f}}, {0.5f, 0.5f})};
    m.setCustomShapes(s);
}

std::vector<float> flatCustomOutput(const std::vector<float>& in, float driveKnob) {
    FilterModule m;
    configureFlatCustom(m, driveKnob);
    return run(m, in);
}

double fundamentalGainDb(const std::vector<float>& out, float peakIn) {
    return db(harmonic(out, kSineHz, 1, kWindow) / peakIn);
}

double thdDb(const std::vector<float>& out) { return db(thd(out, kSineHz, kWindow, 16)); }

} // namespace

TEST(CustomFilterDrive, DriveOneIsTheEngineAloneBitForBit) {
    // The same signal through the bare engine (no Drive stage) and through the module at its default Drive.
    const auto in = sineTone(kSineHz, 0.5f, 8 * kBlock);
    FilterModule m;
    configureFlatCustom(m, 1.0f);
    setParam(m, "cutoff", 440.0f);
    const auto viaModule = run(m, in);

    CustomFilterEngine engine;
    CustomFilterShapes s;
    s.shapes = {makeShape("flat", {{20.0f, 0.0f}, {20000.0f, 0.0f}}, {0.5f, 0.5f})};
    engine.setShapes(s);
    engine.prepare(kSampleRate);
    CustomFilterEngine::Controls c;
    c.resonance = 0.0f;
    std::vector<float> viaEngine = in;
    for (size_t start = 0; start < in.size(); start += kBlock) {
        float* ch[1] = {viaEngine.data() + start};
        engine.process(ch, 1, kBlock, c);
    }
    double worst = 0.0;
    for (size_t i = 0; i < in.size(); ++i)
        worst = std::max(worst, (double)std::abs(viaModule[i] - viaEngine[i]));
    EXPECT_EQ(worst, 0.0) << "Drive 1 must not change a patch that never used Drive";
}

TEST(CustomFilterDrive, StageLeavesSamplesAloneAtDriveOne) {
    CustomFilterDrive stage;
    stage.prepare(kBlock);
    auto x = sineTone(kSineHz, 0.9f, kBlock);
    const auto before = x;
    float* ch[1] = {x.data()};
    stage.process(ch, 1, kBlock, 1.0f, 1.0f);
    EXPECT_EQ(x, before);
}

TEST(CustomFilterDrive, DriveRaisesDistortionAndLevelStepByStep) {
    const auto in = sineTone(kSineHz, 0.1f, kTotal);
    double lastThd = -1000.0, lastGain = -1000.0;
    for (float knob : kKnob) {
        const auto out = flatCustomOutput(in, knob);
        const double t = thdDb(out), g = fundamentalGainDb(out, 0.1f);
        EXPECT_GT(t, lastThd) << "Drive " << knob;
        EXPECT_GT(g, lastGain - 1.0e-6) << "Drive " << knob;
        lastThd = t;
        lastGain = g;
    }
}

// Targets measured from a commercial reference synth's low-pass at a 20 kHz cutoff, resonance 0: a 220 Hz sine
// at two levels through its Drive at 0 / 25 / 50 / 75 / 100 % of travel, which are our knob values 1 / 3.25 /
// 5.5 / 7.75 / 10. Gain is the fundamental's level change in dB; THD is harmonics 2 to 16 against the fundamental.
// The Drive-1 THD entries are skipped: the reference is not transparent there, ours is.
struct DriveTarget {
    float peakIn;
    double gainDb[5];
    double thdDb[5];
};
constexpr DriveTarget kTargets[2] = {
    {0.03f, {-0.02, 7.73, 14.04, 19.31, 23.74}, {-86.3, -70.7, -58.1, -47.4, -38.2}},
    {0.3f, {-0.14, 7.00, 11.20, 12.21, 12.42}, {-46.3, -31.0, -19.0, -11.6, -9.1}},
};
constexpr double kGainToleranceDb = 1.5;
constexpr double kThdToleranceDb = 3.0;

TEST(CustomFilterDrive, KnobCurveSitsOnTheMeasuredTargets) {
    for (const auto& t : kTargets) {
        const auto in = sineTone(kSineHz, t.peakIn, kTotal);
        for (int i = 0; i < 5; ++i) {
            const auto out = flatCustomOutput(in, kKnob[i]);
            EXPECT_NEAR(fundamentalGainDb(out, t.peakIn), t.gainDb[i], kGainToleranceDb)
                << "peak " << t.peakIn << " knob " << kKnob[i];
            if (i > 0)
                EXPECT_NEAR(thdDb(out), t.thdDb[i], kThdToleranceDb) << "peak " << t.peakIn << " knob " << kKnob[i];
        }
    }
}

TEST(CustomFilterDrive, DriveCvMovesItInMonoAndInPoly) {
    const auto in = sineTone(kSineHz, 0.1f, kTotal);
    const auto reference = flatCustomOutput(in, 5.5f); // 1 + 9 * 0.5

    for (const bool poly : {false, true}) {
        FilterModule m;
        configureFlatCustom(m, 1.0f);
        setParam(m, "poly", poly ? 1.0f : 0.0f);
        const int cvChannel = FilterModule::cvChannelFor(2, poly);
        const auto out = run(m, in, [&](int, juce::AudioBuffer<float>& buf) {
            for (int i = 0; i < kBlock; ++i)
                buf.setSample(cvChannel, i, 0.5f);
        });
        EXPECT_NEAR(thdDb(out), thdDb(reference), 0.5) << (poly ? "poly" : "mono");
        EXPECT_NEAR(fundamentalGainDb(out, 0.1f), fundamentalGainDb(reference, 0.1f), 0.5) << (poly ? "poly" : "mono");
        EXPECT_GT(fundamentalGainDb(out, 0.1f), 8.0) << "the CV raised the level";
        EXPECT_NEAR(m.getModulatedDrive(), 5.5f, 1.0e-3f) << "the readout follows the CV";
    }
}

TEST(CustomFilterDrive, PolyVoiceChannelsEachSaturate) {
    // Voice 3's own audio channel (poly: ch3) goes through the stage too, not only voice 0.
    const auto in = sineTone(kSineHz, 0.1f, kTotal);
    FilterModule m;
    configureFlatCustom(m, 10.0f);
    setParam(m, "poly", 1.0f);
    m.prepareToPlay(kSampleRate, kBlock);
    juce::AudioBuffer<float> buf(kChannels, kBlock);
    juce::MidiBuffer midi;
    std::vector<float> out(in.size());
    for (size_t start = 0; start < in.size(); start += kBlock) {
        const int n = (int)std::min<size_t>(kBlock, in.size() - start); // kTotal is not a whole number of blocks
        buf.clear();
        std::copy_n(in.data() + start, n, buf.getWritePointer(3));
        juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), kChannels, n);
        m.processBlock(view, midi);
        std::copy_n(buf.getReadPointer(3), n, out.data() + start);
    }
    EXPECT_GT(fundamentalGainDb(out, 0.1f), 15.0);
}

TEST(CustomFilterDrive, SweepingTheKnobMakesNoStep) {
    // A knob jump from 1 to 10 in one block: the 5 ms smoother turns it into a ramp, so the output never steps
    // by more than it does at steady Drive 10.
    const auto in = sineTone(kSineHz, 0.1f, kTotal);
    FilterModule m;
    configureFlatCustom(m, 1.0f);
    const auto out = run(m, in, [&](int b, juce::AudioBuffer<float>&) {
        if (b == 40)
            setParam(m, "drive", 10.0f);
    });
    auto maxStep = [&](size_t from, size_t to) {
        float s = 0.0f;
        for (size_t i = from + 1; i < to; ++i)
            s = std::max(s, std::abs(out[i] - out[i - 1]));
        return s;
    };
    const float steady = maxStep(kTotal - 8 * kBlock, kTotal);
    const float across = maxStep(38 * kBlock, 44 * kBlock);
    EXPECT_LT(across, 1.25f * steady) << "no click when Drive is turned";
}

TEST(CustomFilterDrive, ProcessingAllocatesNothing) {
    const auto in = sineTone(kSineHz, 0.3f, 4 * kBlock);
    FilterModule m;
    configureFlatCustom(m, 8.0f);
    m.prepareToPlay(kSampleRate, kBlock);
    juce::AudioBuffer<float> buf(kChannels, kBlock);
    juce::MidiBuffer midi;
    buf.clear();
    std::copy_n(in.data(), kBlock, buf.getWritePointer(0));
    m.processBlock(buf, midi); // first block designs the kernel
    AllocationScope scope;
    for (int b = 1; b < 4; ++b) {
        buf.clear();
        std::copy_n(in.data() + b * kBlock, kBlock, buf.getWritePointer(0));
        m.processBlock(buf, midi);
    }
    EXPECT_EQ(scope.count(), 0);
}
