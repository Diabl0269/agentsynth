// Oscillator Pulse Width (square duty cycle) and Glide (portamento) -- the default of each leaves
// today's sound exactly as it was, and each has its own CV jack (ch16/ch17, global params).

#include "Modules/OscillatorModule.h"

#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kSquare = 1;

juce::RangedAudioParameter* param(OscillatorModule& m, const char* id) {
    return dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
}

void setActual(OscillatorModule& m, const char* id, float actual) {
    auto* p = param(m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

float actualOf(OscillatorModule& m, const char* id) {
    auto* p = param(m, id);
    return p != nullptr ? p->convertFrom0to1(p->getValue()) : -12345.0f;
}

void setPoly(OscillatorModule& m, bool on) {
    auto* p = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&m, "poly"));
    ASSERT_NE(p, nullptr);
    *p = on;
}

// Mono: MIDI sets pitch. Poly: ch0 carries the voice-0 pitch in Hz. Both: ch16/ch17 carry the
// Pulse Width / Glide CV. Renders `numBlocks` blocks and returns voice 0's Audio L.
struct Rig {
    OscillatorModule osc;
    bool poly = false;
    float pitchHz = 440.0f;
    float pwCV = 0.0f;
    float glideCV = 0.0f;

    explicit Rig(bool polyMode = false)
        : poly(polyMode) {
        osc.prepareToPlay(kSampleRate, kBlock);
        setPoly(osc, polyMode);
    }

    std::vector<float> render(int numBlocks, int midiNoteOnAtStart = -1) {
        std::vector<float> out;
        for (int b = 0; b < numBlocks; ++b) {
            juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kBlock);
            buf.clear();
            if (poly)
                juce::FloatVectorOperations::fill(buf.getWritePointer(0), pitchHz, kBlock);
            juce::FloatVectorOperations::fill(buf.getWritePointer(16), pwCV, kBlock);
            juce::FloatVectorOperations::fill(buf.getWritePointer(17), glideCV, kBlock);
            juce::MidiBuffer midi;
            if (b == 0 && midiNoteOnAtStart >= 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, midiNoteOnAtStart, (juce::uint8)100), 0);
            osc.processBlock(buf, midi);
            out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
        }
        return out;
    }
};

float polyBlep(float t, float dt) {
    if (t < dt) {
        const float n = t / dt;
        return n + n - n * n - 1.0f;
    }
    if (t > 1.0f - dt) {
        const float n = (t - 1.0f) / dt;
        return n * n + n + n + 1.0f;
    }
    return 0.0f;
}

// The square generator exactly as it was before Pulse Width existed.
float oldSquare(float phase, float dt) {
    float s = phase < 0.5f ? 1.0f : -1.0f;
    s += polyBlep(phase, dt);
    s -= polyBlep(std::fmod(phase + 0.5f, 1.0f), dt);
    return s;
}

float positiveFraction(const std::vector<float>& x, size_t from, size_t to) {
    size_t pos = 0;
    for (size_t i = from; i < to; ++i)
        pos += x[i] > 0.0f ? 1u : 0u;
    return (float)pos / (float)(to - from);
}

int upwardCrossings(const std::vector<float>& x, size_t from, size_t to) {
    int n = 0;
    for (size_t i = from + 1; i < to; ++i)
        n += (x[i - 1] < 0.0f && x[i] >= 0.0f) ? 1 : 0;
    return n;
}

juce::MemoryBlock legacySquareState() {
    // A state blob saved before Pulse Width / Glide existed: it has neither property.
    juce::ValueTree state("ModuleState");
    state.setProperty("waveform", 1.0f / 3.0f, nullptr); // Square
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), block);
    return block;
}

} // namespace

TEST(OscillatorPulseWidth, ParametersExistWithNeutralDefaults) {
    OscillatorModule osc;
    ASSERT_NE(param(osc, "pulseWidth"), nullptr);
    ASSERT_NE(param(osc, "glide"), nullptr);
    EXPECT_NEAR(actualOf(osc, "pulseWidth"), 50.0f, 1e-3f);
    EXPECT_NEAR(actualOf(osc, "glide"), 0.0f, 1e-3f);
    EXPECT_NEAR(param(osc, "pulseWidth")->convertFrom0to1(0.0f), 5.0f, 1e-3f);
    EXPECT_NEAR(param(osc, "pulseWidth")->convertFrom0to1(1.0f), 95.0f, 1e-3f);
    EXPECT_NEAR(param(osc, "glide")->convertFrom0to1(1.0f), 2000.0f, 1e-3f);
}

TEST(OscillatorPulseWidth, DefaultSquareMatchesTheOldSquareSampleForSample) {
    Rig rig;
    setActual(rig.osc, "waveform", (float)kSquare);
    const auto out = rig.render(16);

    float phase = 0.0f;
    const float dt = (float)(440.0 / kSampleRate);
    for (size_t i = 0; i < out.size(); ++i) {
        if (i >= 64) // the first 64 samples crossfade in from the previous waveform
            ASSERT_NEAR(out[i], oldSquare(phase, dt), 1e-5f) << "sample " << i;
        phase += dt;
        if (phase >= 1.0f)
            phase -= 1.0f;
    }
}

TEST(OscillatorPulseWidth, StateWithoutTheNewParamsLoadsAtDefaultsAndRendersTheSame) {
    Rig reference;
    setActual(reference.osc, "waveform", (float)kSquare);
    const auto expected = reference.render(8);

    Rig loaded;
    const auto blob = legacySquareState();
    loaded.osc.setStateInformation(blob.getData(), (int)blob.getSize());
    EXPECT_NEAR(actualOf(loaded.osc, "pulseWidth"), 50.0f, 1e-3f);
    EXPECT_NEAR(actualOf(loaded.osc, "glide"), 0.0f, 1e-3f);
    const auto actual = loaded.render(8);
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); ++i)
        ASSERT_EQ(actual[i], expected[i]) << "sample " << i;
}

TEST(OscillatorPulseWidth, QuarterDutyIsPositiveAQuarterOfTheTime) {
    Rig rig;
    setActual(rig.osc, "waveform", (float)kSquare);
    setActual(rig.osc, "pulseWidth", 25.0f);
    const auto out = rig.render(86); // ~1 s
    EXPECT_NEAR(positiveFraction(out, 2048, out.size()), 0.25f, 0.01f);
}

TEST(OscillatorPulseWidth, OnlyTheSquareIsAffected) {
    Rig narrow, wide;
    setActual(narrow.osc, "pulseWidth", 10.0f); // Sine stays the default waveform
    const auto a = narrow.render(8);
    const auto b = wide.render(8);
    for (size_t i = 0; i < a.size(); ++i)
        ASSERT_EQ(a[i], b[i]) << "sample " << i;
}

TEST(OscillatorPulseWidth, CVJackMovesTheDutyInMonoAndPoly) {
    for (const bool poly : {false, true}) {
        SCOPED_TRACE(poly ? "poly" : "mono");
        Rig rig(poly);
        setActual(rig.osc, "waveform", (float)kSquare);
        rig.pwCV = 0.25f; // +25 % of the knob range from the 50 % centre = 72.5 %
        const auto out = rig.render(86);
        EXPECT_NEAR(positiveFraction(out, 2048, out.size()), 0.725f, 0.02f);
    }
}

TEST(OscillatorPulseWidth, JacksAreDeclaredAndBoundToTheirKnobs) {
    OscillatorModule osc;
    EXPECT_EQ(OscillatorModule::kNumInputs, 18);
    int found = 0;
    for (const auto& poly : {false, true}) {
        setPoly(osc, poly);
        for (const auto& t : osc.getModulationTargets()) {
            if (t.paramId == "pulseWidth") {
                EXPECT_EQ(t.channelIndex, 16);
                ++found;
            }
            if (t.paramId == "glide") {
                EXPECT_EQ(t.channelIndex, 17);
                ++found;
            }
        }
    }
    EXPECT_EQ(found, 4);
}

TEST(OscillatorGlide, ZeroKeepsTheOldFiveMillisecondFrequencySmoothing) {
    Rig rig;
    const auto out = rig.render(2, 81); // A5 on the first block, from the A4 start pitch

    juce::SmoothedValue<float> sm;
    sm.reset(kSampleRate, 0.005);
    sm.setCurrentAndTargetValue(440.0f);
    sm.setTargetValue(880.0f);
    float phase = 0.0f;
    for (size_t i = 0; i < out.size(); ++i) {
        const float freq = juce::jlimit(20.0f, 20000.0f, sm.getNextValue());
        const float dt = (float)(freq / kSampleRate);
        ASSERT_NEAR(out[i], std::sin(phase * juce::MathConstants<float>::twoPi), 1e-4f) << "sample " << i;
        phase += dt;
        if (phase >= 1.0f)
            phase -= 1.0f;
    }
}

namespace {
// Settles on A4, jumps to A5 and returns {crossings in the first 50 ms, crossings 600-650 ms}.
std::pair<int, int> glideJumpCrossings(Rig& rig, int firstNoteOnBlockMidi) {
    rig.render(8);
    const auto out = rig.render(60, firstNoteOnBlockMidi);
    return {upwardCrossings(out, 0, 2205), upwardCrossings(out, 26460, 28665)};
}
} // namespace

TEST(OscillatorGlide, MonoSlidesToTheNewNoteOverTheSetTime) {
    Rig rig;
    setActual(rig.osc, "glide", 500.0f);
    const auto [early, late] = glideJumpCrossings(rig, 81);
    EXPECT_LE(early, 26) << "an instant jump to 880 Hz would count ~44";
    EXPECT_GE(early, 20);
    EXPECT_NEAR(late, 44, 1);
}

TEST(OscillatorGlide, PolyVoiceSlidesAndNeighbourVoicesDoNot) {
    Rig rig(true);
    setActual(rig.osc, "glide", 500.0f);
    rig.render(8);
    rig.pitchHz = 880.0f;
    const auto out = rig.render(60);
    EXPECT_LE(upwardCrossings(out, 0, 2205), 26);
    EXPECT_NEAR(upwardCrossings(out, 26460, 28665), 44, 1);
}

TEST(OscillatorGlide, CVJackSetsTheTime) {
    Rig rig;
    rig.glideCV = 0.25f; // 25 % of 0..2000 ms = 500 ms from a Glide of 0
    const auto [early, late] = glideJumpCrossings(rig, 81);
    EXPECT_LE(early, 26);
    EXPECT_NEAR(late, 44, 1);
}

TEST(OscillatorGlide, FirstNoteOfAFreshPolyVoiceDoesNotSlideFromNothing) {
    Rig rig(true);
    setActual(rig.osc, "glide", 1000.0f);
    rig.pitchHz = 880.0f; // very first pitch the voice ever sees
    const auto out = rig.render(8);
    EXPECT_NEAR(upwardCrossings(out, 0, 2205), 44, 2);
}
