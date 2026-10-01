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

// The wet tail's first sample. Starts looking at `from`: juce::Reverb's dry gain begins at 0.8 and
// ramps to the knob over 10 ms (441 samples), which is not the tail.
int firstWetSample(const std::vector<float>& leg, int from, float threshold = 1.0e-6f) {
    for (size_t i = (size_t)from; i < leg.size(); ++i)
        if (std::abs(leg[i]) > threshold)
            return (int)i;
    return -1;
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

// The module's old behaviour is "juce::Reverb with the knobs' values". At Pre-Delay 0 the output is
// bit-identical to that, first block included.
TEST(ReverbPreDelay, ZeroIsBitIdenticalToThePlainReverb) {
    ReverbModule m;
    const auto actual = render(m, 6);

    juce::Reverb reference;
    reference.setSampleRate(kReverbSr);
    juce::Reverb::Parameters params; // roomSize .5, damping .5, wet .33, dry .4, width 1: the knobs' defaults
    reference.setParameters(params);
    std::vector<float> left, right;
    juce::Random rng(42);
    for (int b = 0; b < 6; ++b) {
        std::vector<float> l(kReverbBlock, 0.0f), r(kReverbBlock, 0.0f);
        if (b == 0)
            for (int i = 0; i < kReverbBlock; ++i) {
                l[(size_t)i] = rng.nextFloat() * 2.0f - 1.0f;
                r[(size_t)i] = l[(size_t)i] * 0.5f;
            }
        reference.setParameters(params);
        reference.processStereo(l.data(), r.data(), kReverbBlock);
        left.insert(left.end(), l.begin(), l.end());
        right.insert(right.end(), r.begin(), r.end());
    }
    for (size_t i = 0; i < left.size(); ++i) {
        ASSERT_EQ(actual[0][i], left[i]) << "left sample " << i;
        ASSERT_EQ(actual[1][i], right[i]) << "right sample " << i;
    }
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
        setReverbParam(m, "dry", 0.0f);
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
        setReverbParam(m, "wet", 0.0f);
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
    EXPECT_EQ(m.getTotalNumInputChannels(), 8);
    EXPECT_EQ(m.getInputPortLabel(6), "Pre-Delay") << "jack 6 of Audio + six CVs, raw ch7";
    const auto targets = m.getModulationTargets();
    ASSERT_EQ(targets.size(), 6u);
    EXPECT_EQ(targets[5].name, "Pre-Delay");
    EXPECT_EQ(targets[5].channelIndex, 7);
    ASSERT_NE(m.parameterForModTarget(targets[5]), nullptr);
    EXPECT_EQ(m.parameterForModTarget(targets[5])->paramID, "preDelay");

    ReverbModule plain;
    setReverbParam(plain, "dry", 0.0f);

    ReverbModule modulated;
    setReverbParam(modulated, "dry", 0.0f);
    // +0.04 of the 0..250 ms range = 10 ms. The CV rides the 50 ms smoothing ramp, so the burst
    // arrives in block 6 (70 ms in), after it has landed.
    const int burstAt = 6 * kReverbBlock;
    const int baseLate = firstWetSample(render(plain, 14, 0.0f, 6)[0], burstAt);
    const int cvOnset = firstWetSample(render(modulated, 14, 0.04f, 6)[0], burstAt);
    EXPECT_EQ(cvOnset - baseLate, 441) << "+0.04 on the jack is 10 ms of pre-delay";
}
