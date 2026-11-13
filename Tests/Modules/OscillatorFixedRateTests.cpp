// Oscillator Rate mode: Follow notes tracks the played note, Fixed runs at the Fixed rate in Hz whatever the note,
// still offset by Octave/Coarse/Fine, in mono and in poly.

#include "AI/AIStateMapper/AIStateMapper.h"
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

// Frequency from the spacing of the upward zero crossings in [from, signal.size()).
double estimateHz(const std::vector<float>& signal, size_t from) {
    std::vector<double> crossings;
    for (size_t i = from + 1; i < signal.size(); ++i)
        if (signal[i - 1] < 0.0f && signal[i] >= 0.0f)
            crossings.push_back((double)(i - 1) + (double)(-signal[i - 1]) / (double)(signal[i] - signal[i - 1]));
    if (crossings.size() < 2)
        return 0.0;
    return (double)(crossings.size() - 1) * kSampleRate / (crossings.back() - crossings.front());
}

struct MonoRig {
    OscillatorModule osc;
    std::vector<float> left;

    MonoRig(bool fixed, float rateHz, int note) {
        osc.prepareToPlay(kSampleRate, kBlock);
        setActual(osc, "rateMode", fixed ? 1.0f : 0.0f);
        setActual(osc, "fixedRate", rateHz);
        noteToPlay = note;
    }

    void render(double seconds) {
        const int blocks = (int)std::ceil(seconds * kSampleRate / kBlock);
        for (int b = 0; b < blocks; ++b) {
            juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kBlock);
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, noteToPlay, (juce::uint8)100), 0);
            osc.processBlock(buf, midi);
            left.insert(left.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
        }
    }

    double hz() const { return estimateHz(left, (size_t)(0.25 * kSampleRate)); }

    int noteToPlay = 60;
};

} // namespace

TEST(OscillatorFixedRate, FixedModeRendersTheRateWhateverTheNote) {
    for (const float rate : {53.0f, 1.0f}) {
        for (const int note : {36, 84}) {
            MonoRig rig(true, rate, note);
            rig.render(rate < 10.0f ? 5.0 : 1.5);
            EXPECT_NEAR(rig.hz(), rate, rate * 0.01) << "rate " << rate << " note " << note;
        }
    }
}

TEST(OscillatorFixedRate, FollowNotesStillTracksTheNote) {
    for (const int note : {48, 72}) {
        MonoRig rig(false, 53.0f, note);
        rig.render(1.0);
        EXPECT_NEAR(rig.hz(), hzOfNote(note), hzOfNote(note) * 0.01) << note;
    }
}

TEST(OscillatorFixedRate, OctaveAndFineStillOffsetTheFixedRate) {
    MonoRig down(true, 53.0f, 60);
    setActual(down.osc, "octave", -1.0f);
    down.render(1.5);
    EXPECT_NEAR(down.hz(), 26.5, 0.5) << "octave -1 halves the rate";

    MonoRig fine(true, 53.0f, 60);
    setActual(fine.osc, "fine", 100.0f);
    fine.render(1.5);
    const double semitoneUp = 53.0 * std::pow(2.0, 1.0 / 12.0);
    EXPECT_NEAR(fine.hz(), semitoneUp, semitoneUp * 0.01) << "fine +100 cents raises a semitone";
}

TEST(OscillatorFixedRate, ASubAudioRateIsNotClampedToTwentyHertz) {
    MonoRig rig(true, 2.0f, 60);
    rig.render(4.0);
    EXPECT_NEAR(rig.hz(), 2.0, 0.05);
}

TEST(OscillatorFixedRate, PolyVoicesStayNoteGatedButPlayTheFixedRate) {
    OscillatorModule osc;
    osc.prepareToPlay(kSampleRate, kBlock);
    setActual(osc, "poly", 1.0f);
    setActual(osc, "rateMode", 1.0f);
    setActual(osc, "fixedRate", 53.0f);

    std::vector<float> voice0, voice1, voice2;
    for (int b = 0; b < 90; ++b) {
        juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kBlock);
        buf.clear();
        for (int s = 0; s < kBlock; ++s) {
            buf.setSample(0, s, 440.0f); // voice 0: live, pitch CV in Hz
            buf.setSample(1, s, 880.0f); // voice 1: live
            // voice 2: pitch CV 0, so it is off
        }
        juce::MidiBuffer midi;
        osc.processBlock(buf, midi);
        voice0.insert(voice0.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
        voice1.insert(voice1.end(), buf.getReadPointer(1), buf.getReadPointer(1) + kBlock);
        voice2.insert(voice2.end(), buf.getReadPointer(2), buf.getReadPointer(2) + kBlock);
    }
    EXPECT_NEAR(estimateHz(voice0, 11025), 53.0, 0.6);
    EXPECT_NEAR(estimateHz(voice1, 11025), 53.0, 0.6);
    float peak = 0.0f;
    for (const float v : voice2)
        peak = std::max(peak, std::abs(v));
    EXPECT_EQ(peak, 0.0f) << "an inactive voice stays silent";
}

TEST(OscillatorFixedRate, StateRoundTripKeepsTheModeAndAStateWithoutThemLoadsAsFollowNotes) {
    OscillatorModule fresh;
    EXPECT_EQ(actualOf(fresh, "rateMode"), 0.0f);
    EXPECT_NEAR(actualOf(fresh, "fixedRate"), 50.0f, 0.01f);

    juce::AudioProcessorGraph source;
    auto node = source.addNode(std::make_unique<OscillatorModule>());
    ASSERT_NE(node, nullptr);
    setActual(*node->getProcessor(), "rateMode", 1.0f);
    setActual(*node->getProcessor(), "fixedRate", 123.0f);
    const juce::var json = synth::AIStateMapper::graphToJSON(source);

    juce::AudioProcessorGraph restored;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, restored, /*clearExisting=*/true, /*trusted=*/true));
    auto& back = *restored.getNodes().getFirst()->getProcessor();
    EXPECT_EQ(actualOf(back, "rateMode"), 1.0f);
    EXPECT_NEAR(actualOf(back, "fixedRate"), 123.0f, 0.5f);

    juce::MemoryBlock state;
    node->getProcessor()->getStateInformation(state);
    OscillatorModule viaBinary;
    viaBinary.setStateInformation(state.getData(), (int)state.getSize());
    EXPECT_EQ(actualOf(viaBinary, "rateMode"), 1.0f);
    EXPECT_NEAR(actualOf(viaBinary, "fixedRate"), 123.0f, 0.5f);

    // A state saved before the parameters existed has neither key.
    juce::var legacy = json;
    auto* nodes = legacy.getProperty("nodes", {}).getArray();
    ASSERT_NE(nodes, nullptr);
    ASSERT_FALSE(nodes->isEmpty());
    for (auto& n : *nodes)
        if (auto* params = n.getProperty("params", {}).getDynamicObject()) {
            params->removeProperty("rateMode");
            params->removeProperty("fixedRate");
        }
    juce::AudioProcessorGraph older;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(legacy, older, /*clearExisting=*/true, /*trusted=*/true));
    auto& loaded = *older.getNodes().getFirst()->getProcessor();
    EXPECT_EQ(actualOf(loaded, "rateMode"), 0.0f);
    EXPECT_NEAR(actualOf(loaded, "fixedRate"), 50.0f, 0.01f);
}

TEST(OscillatorFixedRate, TheAICatalogListsTheRateModeAndTheFixedRateRange) {
    const juce::var catalog = juce::JSON::parse(synth::AIStateMapper::getModuleCatalogJson());
    const auto osc = catalog.getProperty("modules", {}).getProperty("Oscillator", {});
    ASSERT_TRUE(osc.isObject());
    const auto* params = osc.getProperty("params", {}).getArray();
    ASSERT_NE(params, nullptr);
    bool sawMode = false, sawRate = false;
    for (const auto& p : *params) {
        const auto id = p.getProperty("id", {}).toString();
        if (id == "rateMode") {
            sawMode = true;
            const auto* choices = p.getProperty("choices", {}).getArray();
            ASSERT_NE(choices, nullptr);
            ASSERT_EQ(choices->size(), 2);
            EXPECT_EQ((*choices)[0].toString(), "Follow notes");
            EXPECT_EQ((*choices)[1].toString(), "Fixed");
        } else if (id == "fixedRate") {
            sawRate = true;
            EXPECT_NEAR((double)p.getProperty("min", 0), 0.1, 1e-6);
            EXPECT_NEAR((double)p.getProperty("max", 0), 2000.0, 1e-6);
        }
    }
    EXPECT_TRUE(sawMode);
    EXPECT_TRUE(sawRate);
}
