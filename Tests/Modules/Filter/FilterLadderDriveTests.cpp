// The Filter's Drive knob on the ladder types (LPF/HPF/BPF, 12 and 24): the same gain-and-soft-clip stage the Custom
// type has runs ahead of the ladder, so the knob is as strong as the reference synth's. Drive 1 leaves existing
// patches alone, to the last bit, the curve sits on the reference's measured gains, both legs and every poly voice are
// driven, the Drive CV moves it, and Notch still has no Drive.
#include "AllocationCounter.h"
#include "CustomFilterDriveHelpers.h"
#include "FilterLadderDriveGolden.h"
#include "Modules/FilterModule.h"
#include <cstring>
#include <gtest/gtest.h>

using namespace synth::test::drive;

namespace {

constexpr int kTotal = 96000;
constexpr int kWindow = 48000; // 1 s: a whole number of periods of every test tone
constexpr double kSineHz = 220.0;
constexpr float kKnob[5] = {1.0f, 3.25f, 5.5f, 7.75f, 10.0f};
constexpr int kLpf24 = 0, kLpf12 = 1, kHpf12 = 3, kBpf12 = 5, kNotch = 6;

void configure(FilterModule& m, int type, float cutoff, float res, float drive) {
    setParam(m, "filterType", (float)type);
    setParam(m, "cutoff", cutoff);
    setParam(m, "resonance", res);
    setParam(m, "drive", drive);
}

std::vector<float> ladderOutput(int type, const std::vector<float>& in, float drive, float cutoff = 20000.0f) {
    FilterModule m;
    configure(m, type, cutoff, 0.0f, drive);
    return run(m, in);
}

double fundamentalDb(const std::vector<float>& x) { return db(harmonic(x, kSineHz, 1, kWindow)); }

/** Fundamental gain of `drive` over the same type's own Drive 1, in dB. */
double gainOverDriveOneDb(int type, const std::vector<float>& in, float drive) {
    return fundamentalDb(ladderOutput(type, in, drive)) - fundamentalDb(ladderOutput(type, in, 1.0f));
}

/** Runs `in` (on channel `inCh`) through a prepared module and returns channel `inCh`. Block size and rate are free. */
std::vector<float> renderChannel(FilterModule& m, const std::vector<float>& in, int inCh, double rate, int block) {
    m.prepareToPlay(rate, block);
    juce::AudioBuffer<float> buf(kChannels, block);
    juce::MidiBuffer midi;
    std::vector<float> out(in.size());
    for (size_t start = 0; start < in.size(); start += (size_t)block) {
        const int n = (int)std::min<size_t>((size_t)block, in.size() - start);
        buf.clear();
        std::copy_n(in.data() + start, n, buf.getWritePointer(inCh));
        juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), kChannels, n);
        m.processBlock(view, midi);
        std::copy_n(buf.getReadPointer(inCh), n, out.data() + start);
    }
    return out;
}

/** Block 8 of a fixed input, as the golden rows record it: saw on ch0, a sine on the right leg (and, in poly, sines on
    voice 3 and on the right leg's voice 1). Returns the 64 first samples of block 8 of each recorded channel. */
std::vector<std::pair<int, std::vector<uint32_t>>> renderGolden(FilterModule& m, bool poly) {
    setParam(m, "cutoff", 1000.0f);
    setParam(m, "resonance", 0.3f);
    setParam(m, "drive", 1.0f);
    setParam(m, "poly", poly ? 1.0f : 0.0f);
    const int n = 8 * kBlock;
    const auto a = sawTone(110.0, 0.5f, n), b = sineTone(330.0, 0.4f, n), c = sineTone(220.0, 0.3f, n);
    m.prepareToPlay(kSampleRate, kBlock);
    juce::AudioBuffer<float> buf(kChannels, kBlock);
    juce::MidiBuffer midi;
    std::vector<std::pair<int, std::vector<uint32_t>>> rows;
    for (int blk = 0; blk < 8; ++blk) {
        buf.clear();
        std::copy_n(a.data() + blk * kBlock, kBlock, buf.getWritePointer(0));
        std::copy_n(b.data() + blk * kBlock, kBlock, buf.getWritePointer(FilterModule::kRightBase));
        if (poly) {
            std::copy_n(c.data() + blk * kBlock, kBlock, buf.getWritePointer(3));
            std::copy_n(c.data() + blk * kBlock, kBlock, buf.getWritePointer(FilterModule::kRightBase + 1));
        }
        m.processBlock(buf, midi);
    }
    const std::vector<int> channels =
        poly ? std::vector<int>{0, 3, FilterModule::kRightBase, FilterModule::kRightBase + 1}
             : std::vector<int>{0, FilterModule::kRightBase};
    for (int ch : channels) {
        std::vector<uint32_t> bits(64);
        for (int i = 0; i < 64; ++i) {
            const float v = buf.getSample(ch, i);
            std::memcpy(&bits[(size_t)i], &v, 4);
        }
        rows.emplace_back(ch, std::move(bits));
    }
    return rows;
}

} // namespace

// Regression test for FRO2014: the ladder types must sound exactly as they did before Drive got its own stage.
TEST(FilterLadderDrive, DriveOneIsWhatTheLadderAlwaysDidToTheLastBits) {
    int compared = 0;
    for (int type : {kLpf24, kLpf12})
        for (bool poly : {false, true}) {
            FilterModule m;
            setParam(m, "filterType", (float)type);
            const std::string tag = std::string(type == kLpf24 ? "lpf24" : "lpf12") + (poly ? "-poly" : "-mono");
            for (const auto& [ch, bits] : renderGolden(m, poly)) {
                const GoldenRow* row = nullptr;
                for (const auto& g : kGolden)
                    if (tag == g.tag && g.channel == ch)
                        row = &g;
                ASSERT_NE(row, nullptr) << tag << " ch" << ch;
                // Same arithmetic, but another compiler or CPU may round the last bit differently (Linux CI came out
                // one bit off on a few samples), so compare to 1e-6, far below any audible change and far above 1 bit.
                for (int i = 0; i < 64; ++i) {
                    float got = 0.0f, want = 0.0f;
                    std::memcpy(&got, &bits[(size_t)i], 4);
                    std::memcpy(&want, &row->bits[i], 4);
                    EXPECT_NEAR(got, want, 1.0e-6f) << tag << " ch" << ch << " sample " << i;
                }
                ++compared;
            }
        }
    EXPECT_EQ(compared, 12);
}

// Targets measured from the reference synth's low-pass at a 20 kHz cutoff, resonance 0: a 220 Hz sine at two levels
// through its Drive at 0 / 25 / 50 / 75 / 100 % of travel (our knob values 1 / 3.25 / 5.5 / 7.75 / 10), the
// fundamental's level change in dB. The same targets the Custom type is fitted to.
TEST(FilterLadderDrive, KnobCurveSitsOnTheMeasuredTargetsForBothLadderLowPasses) {
    struct Target {
        float peak;
        double gainDb[5];
        double tolerance;
    };
    const Target targets[2] = {{0.03f, {0.0, 7.7, 14.0, 19.3, 23.7}, 1.0}, {0.3f, {0.0, 7.0, 11.2, 12.2, 12.4}, 1.5}};
    for (int type : {kLpf12, kLpf24})
        for (const auto& t : targets) {
            const auto in = sineTone(kSineHz, t.peak, kTotal);
            for (int i = 1; i < 5; ++i)
                EXPECT_NEAR(gainOverDriveOneDb(type, in, kKnob[i]), t.gainDb[i], t.tolerance)
                    << "type " << type << " peak " << t.peak << " knob " << kKnob[i];
        }
}

TEST(FilterLadderDrive, DistortionRisesStepByStep) {
    const auto in = sineTone(kSineHz, 0.1f, kTotal);
    for (int type : {kLpf12, kLpf24}) {
        double last = -1000.0;
        for (float knob : kKnob) {
            const double t = db(thd(ladderOutput(type, in, knob), kSineHz, kWindow, 16));
            EXPECT_GT(t, last) << "type " << type << " knob " << knob;
            last = t;
        }
    }
}

TEST(FilterLadderDrive, HighPassAndBandPassAreDrivenToo) {
    const auto in = sineTone(kSineHz, 0.03f, kTotal);
    for (int type : {kHpf12, kBpf12}) {
        const float cutoff = type == kHpf12 ? 40.0f : (float)kSineHz;
        const double driven = fundamentalDb(ladderOutput(type, in, 10.0f, cutoff));
        const double plain = fundamentalDb(ladderOutput(type, in, 1.0f, cutoff));
        EXPECT_GT(driven - plain, 15.0) << "type " << type;
    }
}

TEST(FilterLadderDrive, TheRightLegIsDrivenInMono) {
    const auto in = sineTone(kSineHz, 0.03f, kTotal);
    auto rightLeg = [&](float drive) {
        FilterModule m;
        configure(m, kLpf12, 20000.0f, 0.0f, drive);
        return renderChannel(m, in, FilterModule::kRightBase, kSampleRate, kBlock);
    };
    EXPECT_GT(fundamentalDb(rightLeg(10.0f)) - fundamentalDb(rightLeg(1.0f)), 20.0);
}

TEST(FilterLadderDrive, EveryPolyVoiceOnBothLegsIsDriven) {
    const auto in = sineTone(kSineHz, 0.03f, kTotal);
    for (int ch : {0, 3, 7, FilterModule::kRightBase, FilterModule::kRightBase + 5}) {
        auto voice = [&](float drive) {
            FilterModule m;
            configure(m, kLpf24, 20000.0f, 0.0f, drive);
            setParam(m, "poly", 1.0f);
            return renderChannel(m, in, ch, kSampleRate, kBlock);
        };
        EXPECT_GT(fundamentalDb(voice(10.0f)) - fundamentalDb(voice(1.0f)), 20.0) << "channel " << ch;
    }
}

TEST(FilterLadderDrive, DriveCvMovesItInMonoAndInPoly) {
    const auto in = sineTone(kSineHz, 0.03f, kTotal);
    const auto reference = ladderOutput(kLpf12, in, 5.5f); // 1 + 9 * 0.5
    for (const bool poly : {false, true}) {
        FilterModule m;
        configure(m, kLpf12, 20000.0f, 0.0f, 1.0f);
        setParam(m, "poly", poly ? 1.0f : 0.0f);
        const int cvChannel = FilterModule::cvChannelFor(2, poly);
        const auto out = run(m, in, [&](int, juce::AudioBuffer<float>& buf) {
            for (int i = 0; i < kBlock; ++i)
                buf.setSample(cvChannel, i, 0.5f);
        });
        EXPECT_NEAR(fundamentalDb(out), fundamentalDb(reference), 0.5) << (poly ? "poly" : "mono");
        EXPECT_GT(fundamentalDb(out) - fundamentalDb(ladderOutput(kLpf12, in, 1.0f)), 10.0)
            << "the CV raised the level";
        EXPECT_NEAR(m.getModulatedDrive(), 5.5f, 1.0e-3f) << "the readout follows the CV";
    }
}

TEST(FilterLadderDrive, NotchStillHasNoDrive) {
    const auto in = sineTone(kSineHz, 0.2f, 8 * kBlock);
    EXPECT_EQ(ladderOutput(kNotch, in, 1.0f, 1000.0f), ladderOutput(kNotch, in, 10.0f, 1000.0f));
}

TEST(FilterLadderDrive, TheCurveDoesNotDependOnBlockSizeOrSampleRate) {
    const auto gainAt = [](double rate, int block) {
        FilterModule m;
        configure(m, kLpf12, 20000.0f, 0.0f, 5.5f);
        const int n = (int)rate; // 1 s
        std::vector<float> in((size_t)n);
        for (int i = 0; i < n; ++i)
            in[(size_t)i] = 0.03f * (float)std::sin(2.0 * juce::MathConstants<double>::pi * kSineHz * i / rate);
        const auto out = renderChannel(m, in, 0, rate, block);
        double re = 0.0, im = 0.0;
        const double w = 2.0 * juce::MathConstants<double>::pi * kSineHz / rate;
        const int len = n / 2; // 0.5 s: 110 whole periods at any rate
        for (int i = n / 2; i < n / 2 + len; ++i) {
            re += out[(size_t)i] * std::cos(w * i);
            im -= out[(size_t)i] * std::sin(w * i);
        }
        return db(2.0 * std::sqrt(re * re + im * im) / len / 0.03);
    };
    const double reference = gainAt(48000.0, 512);
    EXPECT_NEAR(gainAt(48000.0, 64), reference, 0.2);
    EXPECT_NEAR(gainAt(48000.0, 4096), reference, 0.2);
    EXPECT_NEAR(gainAt(44100.0, 512), reference, 0.3);
    EXPECT_NEAR(gainAt(96000.0, 512), reference, 0.3);
}

TEST(FilterLadderDrive, SweepingTheKnobMakesNoStep) {
    // A knob jump from 1 to 10 in one block: the 5 ms smoother turns it into a ramp, so the output never steps by
    // more than it does at steady Drive 10.
    const auto in = sineTone(kSineHz, 0.1f, kTotal);
    FilterModule m;
    configure(m, kLpf12, 20000.0f, 0.0f, 1.0f);
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

TEST(FilterLadderDrive, ProcessingAllocatesNothing) {
    const auto in = sineTone(kSineHz, 0.3f, 4 * kBlock);
    for (const bool poly : {false, true}) {
        FilterModule m;
        configure(m, kLpf12, 3000.0f, 0.3f, 8.0f);
        setParam(m, "poly", poly ? 1.0f : 0.0f);
        m.prepareToPlay(kSampleRate, kBlock);
        juce::AudioBuffer<float> buf(kChannels, kBlock);
        juce::MidiBuffer midi;
        auto fill = [&](int b) {
            buf.clear();
            for (int ch : {0, 1, FilterModule::kRightBase})
                std::copy_n(in.data() + b * kBlock, kBlock, buf.getWritePointer(ch == 1 ? 3 : ch));
        };
        fill(0);
        m.processBlock(buf, midi);
        synth::test::AllocationScope scope;
        for (int b = 1; b < 4; ++b) {
            fill(b);
            m.processBlock(buf, midi);
        }
        EXPECT_EQ(scope.count(), 0) << (poly ? "poly" : "mono");
    }
}
