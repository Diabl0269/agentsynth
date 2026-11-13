// FXModuleReverbTests.cpp — Reverb module coverage
#include "Modules/FX/ReverbModule.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// ---------------------------------------------------------------------------
// ReverbModule tests
// ---------------------------------------------------------------------------

class ReverbModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        module = std::make_unique<ReverbModule>();
        module->prepareToPlay(44100.0, 512);
    }

    std::unique_ptr<ReverbModule> module;
};

TEST_F(ReverbModuleTest, ProcessBlockProducesOutput) {
    // ReverbModule: 2 channels (stereo)
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.5f);

    juce::MidiBuffer midi;
    module->processBlock(buffer, midi);

    // Output should be non-zero (wet + dry signal)
    bool anyNonZero = false;
    for (int i = 0; i < 512; ++i) {
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);
}

TEST_F(ReverbModuleTest, PrepareToPlayAndProcessDoNotCrash) {
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.2f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

TEST_F(ReverbModuleTest, MonoProcessingDoesNotCrash) {
    // Test with a mono buffer (single channel)
    juce::AudioBuffer<float> buffer(1, 512);
    buffer.clear();
    for (int i = 0; i < 512; ++i)
        buffer.setSample(0, i, 0.4f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));

    // Mono reverb should produce non-zero output
    bool anyNonZero = false;
    for (int i = 0; i < 512; ++i) {
        if (buffer.getSample(0, i) != 0.0f) {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);
}

TEST_F(ReverbModuleTest, RoomSizeParameterIsApplied) {
    // Just verify the parameters exist and are the right types
    auto* roomSizeParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module.get(), "roomSize"));
    auto* dampingParam = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module.get(), "damping"));
    ASSERT_NE(roomSizeParam, nullptr);
    ASSERT_NE(dampingParam, nullptr);

    // Change room size and verify processBlock still runs without crash
    roomSizeParam->setValueNotifyingHost(1.0f);
    dampingParam->setValueNotifyingHost(0.8f);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample(ch, i, 0.3f);

    juce::MidiBuffer midi;
    EXPECT_NO_THROW(module->processBlock(buffer, midi));
}

// ---------------------------------------------------------------------------
// Pre-Delay
// ---------------------------------------------------------------------------

namespace {

constexpr double kReverbSr = 44100.0;
constexpr int kReverbBlock = 512;

juce::RangedAudioParameter* reverbParam(ReverbModule& m, const juce::String& id) {
    for (auto* p : m.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p); r != nullptr && r->paramID == id)
            return r;
    return nullptr;
}

void setReverbParam(ReverbModule& m, const juce::String& id, float actual) {
    auto* p = reverbParam(m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

/** Left/right output of `blocks` blocks fed a deterministic noise burst on both legs in block
    `burstBlock` (silence otherwise), through a freshly prepared module. `cvPreDelay` rides the
    Pre-Delay jack (ch7). */
std::array<std::vector<float>, 2> render(ReverbModule& m, int blocks, float cvPreDelay = 0.0f, int burstBlock = 0) {
    m.prepareToPlay(kReverbSr, kReverbBlock);
    juce::AudioBuffer<float> buf(8, kReverbBlock);
    juce::MidiBuffer midi;
    juce::Random rng(42);
    std::array<std::vector<float>, 2> out;
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        for (int i = 0; i < kReverbBlock; ++i) {
            if (b == burstBlock) {
                const float x = rng.nextFloat() * 2.0f - 1.0f;
                buf.setSample(0, i, x);
                buf.setSample(1, i, x * 0.5f);
            }
            buf.setSample(7, i, cvPreDelay);
        }
        m.processBlock(buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kReverbBlock; ++i)
                out[(size_t)ch].push_back(buf.getSample(ch, i));
    }
    return out;
}

// The wet tail's first sample (the tests run at Mix 100%, so no dry signal is in the output).
int firstWetSample(const std::vector<float>& leg, int from, float threshold = 1.0e-6f) {
    for (size_t i = (size_t)from; i < leg.size(); ++i)
        if (std::abs(leg[i]) > threshold)
            return (int)i;
    return -1;
}

/** What the module rendered before Mix existed (Pre-Delay 0): juce::Reverb with the knobs' Wet and Dry values, JUCE's
    own 3x wet and 2x dry scaling included, then the Level. `blocks` blocks of the same noise burst as render(). */
std::array<std::vector<float>, 2> oldEngine(float wet, float dry, float level, int blocks) {
    juce::Reverb reference;
    reference.setSampleRate(kReverbSr);
    juce::Reverb::Parameters params; // roomSize .5, damping .5, width 1: the knobs' defaults
    params.wetLevel = wet;
    params.dryLevel = dry;
    std::array<std::vector<float>, 2> out;
    juce::Random rng(42);
    for (int b = 0; b < blocks; ++b) {
        std::vector<float> l(kReverbBlock, 0.0f), r(kReverbBlock, 0.0f);
        if (b == 0)
            for (int i = 0; i < kReverbBlock; ++i) {
                l[(size_t)i] = rng.nextFloat() * 2.0f - 1.0f;
                r[(size_t)i] = l[(size_t)i] * 0.5f;
            }
        reference.setParameters(params);
        reference.processStereo(l.data(), r.data(), kReverbBlock);
        for (size_t i = 0; i < l.size(); ++i) {
            out[0].push_back(l[i] * level);
            out[1].push_back(r[i] * level);
        }
    }
    return out;
}

float peakRatio(const std::vector<float>& a, const std::vector<float>& b) {
    float pa = 0.0f, pb = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        pa = std::max(pa, std::abs(a[i]));
        pb = std::max(pb, std::abs(b[i]));
    }
    return pb > 0.0f ? pa / pb : 0.0f;
}

} // namespace

TEST(ReverbPreDelay, ParameterRangesFromZeroToTwoHundredFiftyMs) {
    ReverbModule m;
    auto* p = reverbParam(m, "preDelay");
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->convertFrom0to1(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(p->convertFrom0to1(1.0f), 250.0f);
    EXPECT_FLOAT_EQ(p->getValue(), 0.0f);

    std::vector<juce::String> ids;
    for (auto* q : m.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(q))
            ids.push_back(r->paramID);
    const auto pos = [&](const char* id) { return std::find(ids.begin(), ids.end(), juce::String(id)) - ids.begin(); };
    EXPECT_LT(pos("preDelay"), pos("outputLevel"));
    EXPECT_LT(pos("preDelay"), pos("muted"));
}

// Mix 100% is the reverb tail alone at unity tail gain: juce::Reverb run wet-only with its 3x wet factor cancelled.
// At Pre-Delay 0 the output is bit-identical to that.
TEST(ReverbMix, FullMixIsTheBareTailBitForBit) {
    ReverbModule m;
    setReverbParam(m, "mix", 1.0f);
    const auto actual = render(m, 6);
    const auto expected = oldEngine(1.0f / 3.0f, 0.0f, 1.0f, 6);
    // From after juce::Reverb's 10 ms glide from its Parameters() defaults, which the reference has and the module
    // (its gains already settled before the first block) does not.
    for (size_t i = 1024; i < expected[0].size(); ++i) {
        ASSERT_EQ(actual[0][i], expected[0][i]) << "left sample " << i;
        ASSERT_EQ(actual[1][i], expected[1][i]) << "right sample " << i;
    }
}

TEST(ReverbMix, ZeroMixPassesTheInputUntouched) {
    ReverbModule m;
    setReverbParam(m, "mix", 0.0f);
    const auto actual = render(m, 2);
    juce::Random rng(42);
    for (int i = 0; i < kReverbBlock; ++i) {
        const float x = rng.nextFloat() * 2.0f - 1.0f;
        ASSERT_EQ(actual[0][(size_t)i], x) << "left sample " << i;
        ASSERT_EQ(actual[1][(size_t)i], x * 0.5f) << "right sample " << i;
    }
}

TEST(ReverbMix, HasTheSharedMixParameter) {
    ReverbModule m;
    auto* mix = reverbParam(m, "mix");
    ASSERT_NE(mix, nullptr);
    EXPECT_EQ(mix->getName(64), "Mix");
    EXPECT_NEAR(mix->convertFrom0to1(mix->getValue()), ReverbModule::kDefaultMix, 1.0e-6f);
    EXPECT_EQ(mix->getText(0.5f, 16), "50 %") << "percent, like every other Mix";
    EXPECT_EQ(reverbParam(m, "wet"), nullptr);
    EXPECT_EQ(reverbParam(m, "dry"), nullptr);
}

// A project saved with Wet/Dry loads as Mix + Level (AIStateMapper hands the node's params to adoptLegacyParams).
// Wet 0.1 / Dry 0.2 sums to 0.4 + 0.3 = 0.7 <= 1, so the new render equals the old one sample for sample.
TEST(ReverbMix, OldWetDryUnderUnitySumRendersTheSameAsBefore) {
    juce::NamedValueSet saved;
    saved.set("wet", 0.1);
    saved.set("dry", 0.2);
    ReverbModule m;
    m.adoptLegacyParams(saved);
    EXPECT_NEAR(m.getOutputLevel(), 0.7f, 1.0e-6f);
    EXPECT_NEAR(reverbParam(m, "mix")->convertFrom0to1(reverbParam(m, "mix")->getValue()), 0.3f / 0.7f, 1.0e-6f);
    const auto actual = render(m, 6);
    const auto expected = oldEngine(0.1f, 0.2f, 1.0f, 6);
    // juce::Reverb starts its gains at the Parameters() defaults and glides to the knobs over 10 ms; the old module
    // had that glide too, so the comparison starts after it.
    for (size_t i = 1024; i < expected[0].size(); ++i) {
        ASSERT_NEAR(actual[0][i], expected[0][i], 1.0e-5f) << "left sample " << i;
        ASSERT_NEAR(actual[1][i], expected[1][i], 1.0e-5f) << "right sample " << i;
    }
}

// The default pair (Wet 0.33 / Dry 0.4) sums to 1.79 and Level stops at 1, so the migrated project is quieter by
// exactly 1/1.79 (-5.06 dB), the same shape: pinned so the cost of dropping the makeup gain stays the known number.
TEST(ReverbMix, OldDefaultWetDryIsExactlyFivePointOneDecibelsQuieterAndTheSameShape) {
    juce::NamedValueSet saved;
    saved.set("wet", 0.33);
    saved.set("dry", 0.4);
    ReverbModule m;
    m.adoptLegacyParams(saved);
    EXPECT_FLOAT_EQ(m.getOutputLevel(), 1.0f);
    const auto actual = render(m, 6);
    const auto expected = oldEngine(0.33f, 0.4f, 1.0f, 6);
    const float sum = 2.0f * 0.4f + 3.0f * 0.33f;
    for (size_t i = 0; i < expected[0].size(); ++i)
        ASSERT_NEAR(actual[0][i] * sum, expected[0][i], 2.0e-5f) << "left sample " << i;
    EXPECT_NEAR(juce::Decibels::gainToDecibels(peakRatio(actual[0], expected[0])), -5.06f, 0.05f);
}

TEST(ReverbMix, OldNodeWithOnlyWetUsesTheOldDryDefaultAndAnEmptyOrNewNodeIsUntouched) {
    juce::NamedValueSet wetOnly;
    wetOnly.set("wet", 0.5);
    ReverbModule m;
    m.adoptLegacyParams(wetOnly); // a = 2 * 0.4, b = 3 * 0.5
    EXPECT_NEAR(reverbParam(m, "mix")->convertFrom0to1(reverbParam(m, "mix")->getValue()), 1.5f / 2.3f, 1.0e-5f);
    EXPECT_TRUE(m.isLegacyParamId("wet"));
    EXPECT_TRUE(m.isLegacyParamId("dry"));
    EXPECT_FALSE(m.isLegacyParamId("mix"));

    ReverbModule fresh;
    juce::NamedValueSet withMix;
    withMix.set("mix", 0.9);
    withMix.set("wet", 0.1); // a stray old key next to a Mix never overrides it
    fresh.adoptLegacyParams(withMix);
    EXPECT_NEAR(reverbParam(fresh, "mix")->convertFrom0to1(reverbParam(fresh, "mix")->getValue()),
                ReverbModule::kDefaultMix, 1.0e-6f);
    ReverbModule none;
    none.adoptLegacyParams(juce::NamedValueSet());
    EXPECT_FLOAT_EQ(none.getOutputLevel(), 1.0f);
}

// A level the user had set stays: Level = old Level * min(1, a + b).
TEST(ReverbMix, OldLevelIsKeptAndScaledByTheMigration) {
    ReverbModule m;
    setReverbParam(m, "outputLevel", 0.5f);
    juce::NamedValueSet saved;
    saved.set("wet", 0.1);
    saved.set("dry", 0.2);
    m.adoptLegacyParams(saved);
    EXPECT_NEAR(m.getOutputLevel(), 0.5f * 0.7f, 1.0e-6f);
}

// Plugin-host module state written before Mix existed holds wet and dry attributes.
TEST(ReverbMix, OldModuleStateBlobLoadsAsMixAndLevel) {
    juce::ValueTree state("ModuleState");
    state.setProperty("wet", 0.1f, nullptr);
    state.setProperty("dry", 0.2f, nullptr);
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), blob);
    ReverbModule loaded;
    loaded.setStateInformation(blob.getData(), (int)blob.getSize());
    EXPECT_NEAR(loaded.getOutputLevel(), 0.7f, 1.0e-5f);
    EXPECT_NEAR(reverbParam(loaded, "mix")->convertFrom0to1(reverbParam(loaded, "mix")->getValue()), 0.3f / 0.7f,
                1.0e-5f);
}

// The retired Dry channel (raw 5) keeps every later cable where it was, has no jack and takes no cable.
TEST(ReverbMix, RetiredDryChannelKeepsLaterChannelsAndDrawsNoJack) {
    ReverbModule m;
    EXPECT_EQ(m.getTotalNumInputChannels(), 10);
    EXPECT_TRUE(m.isReservedInputChannel(5));
    EXPECT_FALSE(m.isReservedInputChannel(4));
    EXPECT_FALSE(m.isReservedInputChannel(6));
    EXPECT_EQ(m.getVisibleInputPortCount(), 1 + 7) << "Audio + Size, Damping, Mix, Width, Pre-Delay, Level, Pan";
    EXPECT_EQ(m.mapInputChannel(4).visibleJackIndex, 3) << "the Mix jack, where Wet was";
    EXPECT_EQ(m.mapInputChannel(6).visibleJackIndex, 4) << "Width keeps raw channel 6, one jack earlier";
    EXPECT_EQ(m.mapInputChannel(9).visibleJackIndex, 7);
    for (const auto& t : m.getModulationTargets())
        EXPECT_NE(t.channelIndex, 5);
}

TEST(ReverbPreDelay, StateWithoutPreDelayLoadsAtZeroAndRendersTheSame) {
    juce::ValueTree state("ModuleState"); // saved before Pre-Delay existed
    state.setProperty("roomSize", 0.5f, nullptr);
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), blob);

    ReverbModule reference;
    const auto expected = render(reference, 4);
    ReverbModule loaded;
    loaded.setStateInformation(blob.getData(), (int)blob.getSize());
    ASSERT_NE(reverbParam(loaded, "preDelay"), nullptr);
    EXPECT_EQ(reverbParam(loaded, "preDelay")->getValue(), 0.0f);
    const auto actual = render(loaded, 4);
    EXPECT_EQ(actual[0], expected[0]);
    EXPECT_EQ(actual[1], expected[1]);
}

TEST(ReverbPreDelay, ShiftsTheWetOnsetByTheSetTime) {
    auto wetOnly = [](ReverbModule& m, float preDelayMs) {
        setReverbParam(m, "mix", 1.0f);
        setReverbParam(m, "preDelay", preDelayMs);
    };
    ReverbModule plain;
    wetOnly(plain, 0.0f);
    const int base = firstWetSample(render(plain, 20)[0], 500);
    ASSERT_GE(base, 0);

    ReverbModule delayed;
    wetOnly(delayed, 10.0f);
    const int shifted = firstWetSample(render(delayed, 20)[0], 500);
    // The delay line is read with fractional interpolation, so float rounding of 10 ms x 44.1 kHz can put
    // the first audible sample one either side of 441.
    EXPECT_NEAR(shifted - base, 441, 1) << "10 ms at 44.1 kHz";

    ReverbModule longer;
    wetOnly(longer, 100.0f);
    EXPECT_NEAR(firstWetSample(render(longer, 20)[0], 500) - base, 4410, 1);
}

TEST(ReverbPreDelay, TheDrySignalIsNeverDelayed) {
    auto dryOnly = [](ReverbModule& m, float preDelayMs) {
        setReverbParam(m, "mix", 0.0f);
        setReverbParam(m, "preDelay", preDelayMs);
    };
    ReverbModule plain;
    dryOnly(plain, 0.0f);
    const auto a = render(plain, 4);
    ReverbModule delayed;
    dryOnly(delayed, 120.0f);
    const auto b = render(delayed, 4);
    EXPECT_EQ(a[0], b[0]);
    EXPECT_EQ(a[1], b[1]);
}

TEST(ReverbPreDelay, ThePreDelayJackIsKnobBoundAndShiftsTheOnset) {
    ReverbModule m;
    EXPECT_EQ(m.getTotalNumInputChannels(), 10);
    EXPECT_EQ(m.getInputPortLabel(5), "Pre-Delay")
        << "jack 5 of Audio + five CVs (the retired Dry jack is gone), raw ch7";
    const auto targets = m.getModulationTargets();
    ASSERT_EQ(targets.size(), 7u);
    EXPECT_EQ(targets[4].name, "Pre-Delay");
    EXPECT_EQ(targets[4].channelIndex, 7);
    ASSERT_NE(m.parameterForModTarget(targets[4]), nullptr);
    EXPECT_EQ(m.parameterForModTarget(targets[4])->paramID, "preDelay");

    ReverbModule plain;
    setReverbParam(plain, "mix", 1.0f);

    ReverbModule modulated;
    setReverbParam(modulated, "mix", 1.0f);
    // +0.04 of the 0..250 ms range = 10 ms. The CV rides the 50 ms smoothing ramp, so the burst
    // arrives in block 6 (70 ms in), after it has landed.
    const int burstAt = 6 * kReverbBlock;
    const int baseLate = firstWetSample(render(plain, 14, 0.0f, 6)[0], burstAt);
    const int cvOnset = firstWetSample(render(modulated, 14, 0.04f, 6)[0], burstAt);
    EXPECT_EQ(cvOnset - baseLate, 441) << "+0.04 on the jack is 10 ms of pre-delay";
}

// A pre-delay of 10, 20 or 40 ms at 44.1 kHz puts the tap a hair behind the write head once per lap
// of the ring; wrapping that position as a float rounded it to the ring's size, one past the end, and
// the garbage read there turned whole songs into full-scale noise. A sentinel after the ring catches it.
TEST(ReverbPreDelay, TheTapNeverReadsPastTheEndOfTheRing) {
    const double sampleRate = 44100.0;
    const int ringSize = (int)(0.25 * sampleRate) + 4; // ReverbModule's ring: 250 ms plus 4
    std::vector<float> ring((size_t)ringSize + 1, 0.0f);
    ring[(size_t)ringSize] = 1.0e30f;

    for (int tenthMs = 0; tenthMs <= 2500; ++tenthMs) {
        const float delaySamples = (float)tenthMs * 0.1f * 0.001f * (float)sampleRate; // as renderChunk computes it
        for (int writePos = 0; writePos < ringSize; ++writePos)
            ASSERT_EQ(ReverbModule::readPreDelayed(ring.data(), ringSize, writePos, delaySamples), 0.0f)
                << "pre-delay " << tenthMs / 10.0 << " ms, write position " << writePos;
    }
}

TEST(ReverbPreDelay, TheTapInterpolatesBetweenTheTwoSamplesAroundIt) {
    std::vector<float> ring(100);
    for (int i = 0; i < 100; ++i)
        ring[(size_t)i] = (float)i;
    EXPECT_FLOAT_EQ(ReverbModule::readPreDelayed(ring.data(), 100, 50, 10.25f), 39.75f);
    EXPECT_FLOAT_EQ(ReverbModule::readPreDelayed(ring.data(), 100, 50, 10.0f), 40.0f);
    // Across the wrap: 3 behind position 1 is 98, a quarter of the way on to 97.
    EXPECT_FLOAT_EQ(ReverbModule::readPreDelayed(ring.data(), 100, 1, 3.25f), 97.75f);
}
