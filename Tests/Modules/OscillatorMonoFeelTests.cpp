// Oscillator mono voice feel: Glide Mode (Scaled), Glide Curve (Exponential) and Legato.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/Oscillator/PitchGlide.h"
#include "Modules/OscillatorModule.h"

#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 512;

juce::RangedAudioParameter* param(juce::AudioProcessor& m, const char* id) {
    return dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
}

void setActual(juce::AudioProcessor& m, const char* id, float actual) {
    auto* p = param(m, id);
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

float actualOf(juce::AudioProcessor& m, const char* id) {
    auto* p = param(m, id);
    return p != nullptr ? p->convertFrom0to1(p->getValue()) : -12345.0f;
}

float hzOfNote(int note) { return 440.0f * std::pow(2.0f, ((float)note - 69.0f) / 12.0f); }

// Samples until the glide has landed (a generous cap stops a glide that never ends).
int samplesToLand(synth::PitchGlide& glide) {
    int n = 0;
    while (glide.isGliding() && n < 1000000) {
        glide.nextFrequency();
        ++n;
    }
    return n;
}

struct NoteEvent {
    int block;
    int offset;
    int note;
    bool on;
};

struct Rig {
    OscillatorModule osc;
    std::vector<float> left;

    Rig() { osc.prepareToPlay(kSampleRate, kBlock); }

    void renderBlock(int blockIndex, const std::vector<NoteEvent>& events) {
        juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kBlock);
        buf.clear();
        juce::MidiBuffer midi;
        for (const auto& e : events)
            if (e.block == blockIndex)
                midi.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, (juce::uint8)100)
                                   : juce::MidiMessage::noteOff(1, e.note),
                              e.offset);
        osc.processBlock(buf, midi);
        left.insert(left.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
    }

    void render(int numBlocks, const std::vector<NoteEvent>& events) {
        for (int b = 0; b < numBlocks; ++b)
            renderBlock(b, events);
    }

    // Frequency from the spacing of the upward zero crossings in [from, to).
    double estimateHz(size_t from, size_t to) const {
        std::vector<double> crossings;
        for (size_t i = from + 1; i < to; ++i)
            if (left[i - 1] < 0.0f && left[i] >= 0.0f)
                crossings.push_back((double)(i - 1) + (double)(-left[i - 1]) / (double)(left[i] - left[i - 1]));
        if (crossings.size() < 2)
            return 0.0;
        return (double)(crossings.size() - 1) * kSampleRate / (crossings.back() - crossings.front());
    }
};

} // namespace

// ---- PitchGlide -----------------------------------------------------------------------------------

TEST(PitchGlideModes, AlwaysTakesTheSetTimeForAnyInterval) {
    for (const float target : {880.0f, 466.16f}) {
        synth::PitchGlide glide;
        glide.retarget(440.0f, 0.1f, kSampleRate);
        glide.retarget(target, 0.1f, kSampleRate);
        EXPECT_NEAR(samplesToLand(glide), 4410, 3) << target;
    }
}

TEST(PitchGlideModes, ScaledTakesTheSetTimePerOctave) {
    synth::PitchGlide octave;
    octave.retarget(220.0f, 0.1f, kSampleRate, synth::GlideTime::Scaled);
    octave.retarget(440.0f, 0.1f, kSampleRate, synth::GlideTime::Scaled);
    EXPECT_NEAR(samplesToLand(octave), 4410, 3);

    synth::PitchGlide semitone;
    semitone.retarget(440.0f, 0.1f, kSampleRate, synth::GlideTime::Scaled);
    semitone.retarget(440.0f * std::pow(2.0f, 1.0f / 12.0f), 0.1f, kSampleRate, synth::GlideTime::Scaled);
    EXPECT_NEAR(samplesToLand(semitone), 4410 / 12, 3);
}

TEST(PitchGlideModes, ExponentialLeadsLinearAtTheMidpointAndLandsOnTarget) {
    synth::PitchGlide linear, curved;
    for (auto* g : {&linear, &curved})
        g->retarget(220.0f, 0.1f, kSampleRate);
    linear.retarget(440.0f, 0.1f, kSampleRate, synth::GlideTime::Always, synth::GlideCurve::Linear);
    curved.retarget(440.0f, 0.1f, kSampleRate, synth::GlideTime::Always, synth::GlideCurve::Exponential);

    float linearMid = 0.0f, curvedMid = 0.0f;
    for (int i = 0; i < 2205; ++i) {
        linearMid = linear.nextFrequency();
        curvedMid = curved.nextFrequency();
    }
    EXPECT_GT(curvedMid, linearMid) << "fast start: the exponential is nearer the target halfway through";

    int n = 2205;
    float last = curvedMid;
    while (curved.isGliding() && n < 100000) {
        last = curved.nextFrequency();
        ++n;
    }
    EXPECT_LE(n, 4412) << "it is done by the set time";
    EXPECT_NEAR(curved.nextFrequency(), 440.0f, 1e-3f) << "and sits exactly on the target";
    EXPECT_GT(last, 439.0f);
}

// ---- Module ---------------------------------------------------------------------------------------

TEST(OscillatorMonoFeel, NewParametersDefaultToTodaysBehaviourAndSurviveSaveAndLoad) {
    OscillatorModule osc;
    EXPECT_EQ(actualOf(osc, "glideMode"), 0.0f);
    EXPECT_EQ(actualOf(osc, "glideCurve"), 0.0f);
    EXPECT_EQ(actualOf(osc, "legato"), 0.0f);

    juce::AudioProcessorGraph source;
    auto node = source.addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(node, nullptr);
    setActual(*node->getProcessor(), "glideMode", 1.0f);
    setActual(*node->getProcessor(), "glideCurve", 1.0f);
    setActual(*node->getProcessor(), "legato", 1.0f);
    const juce::var json = synth::AIStateMapper::graphToJSON(source);
    juce::AudioProcessorGraph restored;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, restored, /*clearExisting=*/true, /*trusted=*/true));
    auto& back = *restored.getNodes().getFirst()->getProcessor();
    EXPECT_EQ(actualOf(back, "glideMode"), 1.0f);
    EXPECT_EQ(actualOf(back, "glideCurve"), 1.0f);
    EXPECT_EQ(actualOf(back, "legato"), 1.0f);

    juce::MemoryBlock state;
    node->getProcessor()->getStateInformation(state);
    OscillatorModule viaBinary;
    viaBinary.setStateInformation(state.getData(), (int)state.getSize());
    EXPECT_EQ(actualOf(viaBinary, "glideMode"), 1.0f);
    EXPECT_EQ(actualOf(viaBinary, "legato"), 1.0f);
}

TEST(OscillatorMonoFeel, ScaledGlideModeShortensASemitoneSlide) {
    auto settledHz = [](float mode) {
        Rig rig;
        setActual(rig.osc, "glide", 400.0f);
        setActual(rig.osc, "glideMode", mode);
        rig.render(8, {{0, 0, 69, true}, {2, 0, 70, true}});
        return rig.estimateHz((size_t)(6 * kBlock), (size_t)(8 * kBlock));
    };
    EXPECT_NEAR(settledHz(1.0f), hzOfNote(70), hzOfNote(70) * 0.01) << "a semitone takes 400/12 ms";
    EXPECT_LT(settledHz(0.0f), hzOfNote(70) * 0.97) << "Always is still sliding at 70 ms";
}

TEST(OscillatorMonoFeel, LegatoJumpsForANoteThatIsNotOverlappingAndGlidesForOneThatIs) {
    auto firstBlockHz = [](bool legato, bool overlap) {
        Rig rig;
        setActual(rig.osc, "glide", 400.0f);
        setActual(rig.osc, "legato", legato ? 1.0f : 0.0f);
        std::vector<NoteEvent> notes = {{0, 0, 60, true}};
        if (!overlap)
            notes.push_back({3, 0, 60, false});
        notes.push_back({4, 0, 72, true});
        rig.render(6, notes);
        return rig.estimateHz((size_t)(4 * kBlock + 64), (size_t)(5 * kBlock));
    };
    EXPECT_NEAR(firstBlockHz(true, false), hzOfNote(72), hzOfNote(72) * 0.02) << "legato: a fresh note jumps";
    EXPECT_LT(firstBlockHz(true, true), hzOfNote(72) * 0.7) << "legato: an overlapping note slides";
    EXPECT_LT(firstBlockHz(false, false), hzOfNote(72) * 0.7) << "no legato: every note slides, as before";
}

TEST(OscillatorMonoFeel, LegatoKeepsThePhaseThroughAnOverlappingNote) {
    auto render = [](bool legato, float randAfterFirstNote) {
        Rig rig;
        setActual(rig.osc, "legato", legato ? 1.0f : 0.0f);
        const std::vector<NoteEvent> notes = {{0, 0, 60, true}, {4, 100, 64, true}};
        for (int b = 0; b < 8; ++b) {
            if (b == 2)
                setActual(rig.osc, "randomPhase", randAfterFirstNote);
            rig.renderBlock(b, notes);
        }
        return rig.left;
    };
    EXPECT_EQ(render(true, 0.0f), render(true, 100.0f)) << "no restart, so Rand never gets a say";
    EXPECT_NE(render(false, 0.0f), render(false, 100.0f)) << "without legato the overlapping note restarts";
}

TEST(OscillatorMonoFeel, LegatoReturnsToTheHeldNoteWhenTheNewestIsReleased) {
    Rig rig;
    setActual(rig.osc, "glide", 30.0f);
    setActual(rig.osc, "legato", 1.0f);
    rig.render(14, {{0, 0, 60, true}, {3, 0, 72, true}, {8, 0, 72, false}});
    EXPECT_NEAR(rig.estimateHz((size_t)(6 * kBlock), (size_t)(8 * kBlock)), hzOfNote(72), hzOfNote(72) * 0.02);
    EXPECT_NEAR(rig.estimateHz((size_t)(12 * kBlock), (size_t)(14 * kBlock)), hzOfNote(60), hzOfNote(60) * 0.02)
        << "back on the held note";
}

// ---- Blend and Width CV jacks (raw channels 18 and 19, the same in both voice modes) --------------------

namespace {
// Renders a few blocks of 5-voice unison with `cvValue` on `cvChannel`; returns {left, right}.
std::pair<std::vector<float>, std::vector<float>> renderWithCV(bool poly, int cvChannel, float cvValue, float blend,
                                                               float width) {
    OscillatorModule osc;
    setActual(osc, "poly", poly ? 1.0f : 0.0f);
    setActual(osc, "unison", 5.0f);
    setActual(osc, "detune", 50.0f);
    setActual(osc, "blend", blend);
    setActual(osc, "width", width);
    osc.prepareToPlay(kSampleRate, kBlock);
    std::vector<float> left, right;
    for (int b = 0; b < 4; ++b) {
        juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kBlock);
        buf.clear();
        if (poly)
            juce::FloatVectorOperations::fill(buf.getWritePointer(0), 440.0f, kBlock);
        if (cvChannel >= 0)
            juce::FloatVectorOperations::fill(buf.getWritePointer(cvChannel), cvValue, kBlock);
        juce::MidiBuffer midi;
        osc.processBlock(buf, midi);
        const int r = OscillatorModule::kRightBase;
        left.insert(left.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
        right.insert(right.end(), buf.getReadPointer(r), buf.getReadPointer(r) + kBlock);
    }
    return {left, right};
}

// Largest difference; `from` skips the start, where a smoothed Blend/Width is still ramping to the CV'd value.
float maxDifference(const std::vector<float>& a, const std::vector<float>& b, size_t from = 0) {
    float d = 0.0f;
    for (size_t i = from; i < a.size(); ++i)
        d = std::max(d, std::abs(a[i] - b[i]));
    return d;
}
} // namespace

TEST(OscillatorBlendWidthCV, JacksAreDeclaredOnChannels18And19InBothModes) {
    OscillatorModule osc;
    EXPECT_EQ(OscillatorModule::kNumInputs, 29);
    for (const bool poly : {false, true}) {
        setActual(osc, "poly", poly ? 1.0f : 0.0f);
        int blend = -1, width = -1;
        for (const auto& t : osc.getModulationTargets()) {
            if (t.paramId == "blend")
                blend = t.channelIndex;
            if (t.paramId == "width")
                width = t.channelIndex;
        }
        EXPECT_EQ(blend, 18) << poly;
        EXPECT_EQ(width, 19) << poly;
    }
}

TEST(OscillatorBlendWidthCV, CVOnTheBlendJackMovesBlend) {
    for (const bool poly : {false, true}) {
        const auto plain = renderWithCV(poly, -1, 0.0f, 1.0f, 0.5f);
        const auto driven = renderWithCV(poly, 18, -1.0f, 1.0f, 0.5f);
        EXPECT_GT(maxDifference(plain.first, driven.first), 0.01f) << poly;
        // The same value set on the knob matches what the jack produced.
        const auto viaKnob = renderWithCV(poly, -1, 0.0f, 0.0f, 0.5f);
        EXPECT_LT(maxDifference(driven.first, viaKnob.first, 3 * kBlock), 0.02f) << poly;
    }
}

TEST(OscillatorBlendWidthCV, CVOnTheWidthJackMovesWidth) {
    for (const bool poly : {false, true}) {
        const auto narrow = renderWithCV(poly, -1, 0.0f, 1.0f, 0.0f);
        const auto driven = renderWithCV(poly, 19, 1.0f, 1.0f, 0.0f);
        EXPECT_GT(maxDifference(narrow.first, driven.first) + maxDifference(narrow.second, driven.second), 0.01f)
            << poly;
        const auto viaKnob = renderWithCV(poly, -1, 0.0f, 1.0f, 1.0f);
        EXPECT_LT(maxDifference(driven.first, viaKnob.first, 3 * kBlock), 0.02f) << poly;
        EXPECT_LT(maxDifference(driven.second, viaKnob.second, 3 * kBlock), 0.02f) << poly;
    }
}
