// LFO Phase (start-phase offset) and Fade In (amplitude ramp after a retrigger). Both default to
// "off" and then leave the output exactly as it was.

#include "Modules/LFOModule.h"

#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 512;

juce::RangedAudioParameter* param(LFOModule& m, const char* id) {
    return dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
}

void setActual(LFOModule& m, const char* id, float actual) {
    auto* p = param(m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

void setBool(LFOModule& m, const char* id, bool on) {
    auto* p = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    *p = on;
}

void setShape(LFOModule& m, int index) {
    auto* p = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&m, "shape"));
    ASSERT_NE(p, nullptr);
    *p = index;
}

struct Rig {
    LFOModule lfo;
    float phaseCV = 0.0f;
    float fadeCV = 0.0f;

    Rig() {
        lfo.prepareToPlay(kSampleRate, kBlock);
        setBool(lfo, "mode", false); // Hz mode: the rate is the rateHz knob, no tempo involved
        setActual(lfo, "rateHz", 2.0f);
    }

    std::vector<float> render(int numBlocks, bool noteOnFirstBlock = false) {
        std::vector<float> out;
        for (int b = 0; b < numBlocks; ++b) {
            juce::AudioBuffer<float> buf(LFOModule::kNumInputs, kBlock);
            buf.clear();
            if (LFOModule::kNumInputs > 3)
                juce::FloatVectorOperations::fill(buf.getWritePointer(3), phaseCV, kBlock);
            if (LFOModule::kNumInputs > 4)
                juce::FloatVectorOperations::fill(buf.getWritePointer(4), fadeCV, kBlock);
            juce::MidiBuffer midi;
            if (b == 0 && noteOnFirstBlock)
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
            lfo.processBlock(buf, midi);
            out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
        }
        return out;
    }
};

juce::MemoryBlock legacyState() {
    juce::ValueTree state("ModuleState"); // saved before Phase / Fade In existed
    state.setProperty("mode", 0.0f, nullptr);
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), block);
    return block;
}

} // namespace

TEST(LFOPhase, ParametersExistWithNeutralDefaults) {
    LFOModule lfo;
    ASSERT_NE(param(lfo, "phase"), nullptr);
    ASSERT_NE(param(lfo, "fadeIn"), nullptr);
    EXPECT_NEAR(param(lfo, "phase")->convertFrom0to1(param(lfo, "phase")->getValue()), 0.0f, 1e-3f);
    EXPECT_NEAR(param(lfo, "phase")->convertFrom0to1(1.0f), 360.0f, 1e-3f);
    EXPECT_NEAR(param(lfo, "fadeIn")->convertFrom0to1(param(lfo, "fadeIn")->getValue()), 0.0f, 1e-3f);
    EXPECT_NEAR(param(lfo, "fadeIn")->convertFrom0to1(1.0f), 10000.0f, 1e-3f);
}

TEST(LFOPhase, DefaultSineMatchesTheOldPhaseAccumulatorExactly) {
    Rig rig;
    const auto out = rig.render(8);
    float phase = 0.0f;
    const float inc =
        param(rig.lfo, "rateHz")->convertFrom0to1(param(rig.lfo, "rateHz")->getValue()) / (float)kSampleRate;
    for (size_t i = 0; i < out.size(); ++i) {
        ASSERT_EQ(out[i], std::sin(phase * juce::MathConstants<float>::twoPi)) << "sample " << i;
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;
    }
}

TEST(LFOPhase, StateWithoutTheNewParamsLoadsAtDefaultsAndRendersTheSame) {
    Rig reference;
    const auto expected = reference.render(8);

    Rig loaded;
    const auto blob = legacyState();
    loaded.lfo.setStateInformation(blob.getData(), (int)blob.getSize());
    setActual(loaded.lfo, "rateHz", 2.0f);
    const auto actual = loaded.render(8);
    ASSERT_EQ(actual, expected);
}

TEST(LFOPhase, NinetyDegreesStartsAtThePeakOfASine) {
    Rig rig;
    setActual(rig.lfo, "phase", 90.0f);
    const auto out = rig.render(1);
    EXPECT_NEAR(out[0], 1.0f, 1e-3f);
}

TEST(LFOPhase, HalfTurnStartsASquareInverted) {
    Rig rig;
    setShape(rig.lfo, 3); // Square: +1 for the first half of the cycle
    EXPECT_GT(rig.render(1)[0], 0.0f);

    Rig shifted;
    setShape(shifted.lfo, 3);
    setActual(shifted.lfo, "phase", 180.0f);
    EXPECT_LT(shifted.render(1)[0], 0.0f);
}

TEST(LFOPhase, RetrigRestartsAtTheOffsetNotAtZero) {
    Rig rig;
    setShape(rig.lfo, 3);
    setBool(rig.lfo, "retrig", true);
    setActual(rig.lfo, "phase", 180.0f);
    rig.render(100); // free-run well away from the start
    const auto out = rig.render(1, /*noteOn*/ true);
    EXPECT_LT(out[0], 0.0f) << "a restart lands on the offset, i.e. the inverted half";
}

TEST(LFOPhase, OffsetAppliesToTheFreeRunningPhaseWhenRetrigIsOff) {
    Rig base, shifted;
    setShape(base.lfo, 3);
    setShape(shifted.lfo, 3);
    setActual(shifted.lfo, "phase", 180.0f);
    const auto a = base.render(40);
    const auto b = shifted.render(40);
    // Away from the edges, a half-turn offset is an exact inversion of the square.
    int checked = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const float ph = std::fmod(2.0f * (float)i / (float)kSampleRate, 1.0f);
        if (std::abs(ph - 0.5f) < 0.01f || ph < 0.01f || ph > 0.99f)
            continue;
        ASSERT_EQ(a[i], -b[i]) << "sample " << i;
        ++checked;
    }
    EXPECT_GT(checked, 1000);
}

TEST(LFOPhase, PhaseCVJackMovesTheOffset) {
    Rig rig;
    rig.phaseCV = 0.25f; // +90 degrees of the 0..360 range
    const auto out = rig.render(1);
    EXPECT_NEAR(out[0], 1.0f, 1e-3f);
}

TEST(LFOFadeIn, DefaultHasNoRamp) {
    Rig rig;
    setShape(rig.lfo, 3);
    setBool(rig.lfo, "retrig", true);
    const auto out = rig.render(1, true);
    EXPECT_FLOAT_EQ(out[0], 1.0f);
    EXPECT_FLOAT_EQ(out[1], 1.0f);
}

TEST(LFOFadeIn, RampsFromZeroToFullOverTheSetTimeAfterANoteOn) {
    Rig rig;
    setShape(rig.lfo, 3); // square: |output| is the ramp itself
    setBool(rig.lfo, "retrig", true);
    setActual(rig.lfo, "fadeIn", 100.0f); // 4410 samples
    const auto out = rig.render(12, true);
    EXPECT_NEAR(std::abs(out[0]), 0.0f, 1e-3f);
    EXPECT_NEAR(std::abs(out[441]), 0.1f, 0.01f);
    EXPECT_NEAR(std::abs(out[2205]), 0.5f, 0.01f);
    EXPECT_NEAR(std::abs(out[4600]), 1.0f, 1e-3f);
}

TEST(LFOFadeIn, EachNewNoteRestartsTheRamp) {
    Rig rig;
    setShape(rig.lfo, 3);
    setBool(rig.lfo, "retrig", true);
    setActual(rig.lfo, "fadeIn", 100.0f);
    rig.render(12, true);
    const auto out = rig.render(1, true);
    EXPECT_NEAR(std::abs(out[0]), 0.0f, 1e-3f);
}

TEST(LFOFadeIn, RestartOffMeansNoNoteAwarenessSoNoRamp) {
    Rig rig;
    setShape(rig.lfo, 3);
    setActual(rig.lfo, "fadeIn", 100.0f); // Retrig stays off
    const auto out = rig.render(1, true);
    EXPECT_FLOAT_EQ(out[0], 1.0f);
}

TEST(LFOFadeIn, CVJackSetsTheTime) {
    Rig rig;
    setShape(rig.lfo, 3);
    setBool(rig.lfo, "retrig", true);
    rig.fadeCV = 0.01f; // 1 % of 0..10000 ms = 100 ms
    const auto out = rig.render(12, true);
    EXPECT_NEAR(std::abs(out[441]), 0.1f, 0.01f);
    EXPECT_NEAR(std::abs(out[4600]), 1.0f, 1e-3f);
}

TEST(LFOFadeIn, UnipolarFadesTheOutputUpFromZero) {
    Rig rig;
    setShape(rig.lfo, 3);
    setBool(rig.lfo, "bipolar", false);
    setBool(rig.lfo, "retrig", true);
    setActual(rig.lfo, "fadeIn", 100.0f);
    const auto out = rig.render(12, true);
    EXPECT_NEAR(out[0], 0.0f, 1e-3f);
    EXPECT_NEAR(out[2205], 0.5f, 0.01f);
    EXPECT_NEAR(out[4600], 1.0f, 1e-3f);
}
