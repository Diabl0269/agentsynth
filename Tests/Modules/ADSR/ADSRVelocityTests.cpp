// ADSR Velocity: how far the note's velocity scales the envelope output. 0 % ignores velocity
// (today's behaviour). The velocity source is the MIDI note-on velocity in mono and the gate level
// at the rising edge on a Gate CV (which is the velocity when Poly MIDI's "Velocity sets gate" is on).

#include "ADSRTestFixture.h"
#include "Modules/PolyMidiModule.h"

namespace {

float peakOf(const juce::AudioBuffer<float>& b, int ch, int from = 0) {
    float peak = 0.0f;
    for (int i = from; i < b.getNumSamples(); ++i)
        peak = std::max(peak, std::abs(b.getSample(ch, i)));
    return peak;
}

// Fast attack, full sustain, so the held output sits at exactly the (scaled) level.
void fastEnvelope(ADSRModule& m) {
    setFloat(m, "attack", 0.0f);
    setFloat(m, "decay", 0.05f);
    setFloat(m, "sustain", 1.0f);
}

juce::MemoryBlock legacyState() {
    juce::ValueTree state("ModuleState"); // saved before Velocity existed
    state.setProperty("attack", 0.0f, nullptr);
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), block);
    return block;
}

// One mono block with a MIDI note-on of `velocity` (0..127), returns the peak of the second half.
float monoMidiPeak(ADSRModule& m, int velocity) {
    juce::AudioBuffer<float> buf(2, 2048);
    buf.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)velocity), 0);
    m.processBlock(buf, midi);
    return peakOf(buf, 0, 1024);
}

} // namespace

TEST_F(ADSRTest, VelocityParameterExistsAtZero) {
    auto* p = floatParam(adsr, "velocity");
    ASSERT_NE(p, nullptr);
    EXPECT_NEAR(p->get(), 0.0f, 1e-6f);
    EXPECT_NEAR(p->convertFrom0to1(1.0f), 100.0f, 1e-3f);
}

TEST_F(ADSRTest, VelocityZeroIgnoresVelocity) {
    fastEnvelope(adsr);
    ASSERT_NE(floatParam(adsr, "velocity"), nullptr);
    EXPECT_NEAR(monoMidiPeak(adsr, 20), 1.0f, 1e-3f);
}

TEST_F(ADSRTest, StateWithoutVelocityLoadsAtZeroAndRendersTheSame) {
    ADSRModule reference, loaded;
    reference.prepareToPlay(44100.0, 512);
    loaded.prepareToPlay(44100.0, 512);
    const auto blob = legacyState();
    loaded.setStateInformation(blob.getData(), (int)blob.getSize());
    ASSERT_NE(floatParam(loaded, "velocity"), nullptr);
    EXPECT_NEAR(floatParam(loaded, "velocity")->get(), 0.0f, 1e-6f);
    setFloat(reference, "attack", 0.0f);

    juce::AudioBuffer<float> a(2, 1024), b(2, 1024);
    a.clear();
    b.clear();
    juce::MidiBuffer m1, m2;
    m1.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)30), 0);
    m2.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)30), 0);
    reference.processBlock(a, m1);
    loaded.processBlock(b, m2);
    for (int i = 0; i < 1024; ++i)
        ASSERT_EQ(a.getSample(0, i), b.getSample(0, i)) << "sample " << i;
}

TEST_F(ADSRTest, FullVelocityScalesTheOutputByTheNoteVelocity) {
    fastEnvelope(adsr);
    setFloat(adsr, "velocity", 100.0f);
    const float soft = monoMidiPeak(adsr, 40);
    EXPECT_NEAR(soft, 40.0f / 127.0f, 0.01f);

    ADSRModule hard;
    hard.prepareToPlay(44100.0, 512);
    fastEnvelope(hard);
    setFloat(hard, "velocity", 100.0f);
    const float loud = monoMidiPeak(hard, 127);
    EXPECT_NEAR(loud, 1.0f, 0.01f);
    EXPECT_LT(soft, loud);
}

TEST_F(ADSRTest, HalfVelocityBlendsBetweenFlatAndVelocityScaled) {
    fastEnvelope(adsr);
    setFloat(adsr, "velocity", 50.0f);
    const float vel = 40.0f / 127.0f;
    EXPECT_NEAR(monoMidiPeak(adsr, 40), 0.5f + 0.5f * vel, 0.01f);
}

TEST_F(ADSRTest, GateLevelIsTheVelocityWhenNoMidiNoteOnDrivesIt) {
    fastEnvelope(adsr);
    setFloat(adsr, "velocity", 100.0f);
    juce::AudioBuffer<float> buf(2, 2048);
    buf.clear();
    juce::FloatVectorOperations::fill(buf.getWritePointer(0), 0.6f, 2048);
    juce::MidiBuffer midi;
    adsr.processBlock(buf, midi);
    EXPECT_NEAR(peakOf(buf, 0, 1024), 0.6f, 0.01f);
}

TEST_F(ADSRTest, PolyVoicesEachKeepTheirOwnVelocity) {
    fastEnvelope(adsr);
    setPoly(adsr, true);
    setFloat(adsr, "velocity", 100.0f);
    juce::AudioBuffer<float> buf(14, 2048);
    buf.clear();
    juce::FloatVectorOperations::fill(buf.getWritePointer(0), 0.6f, 2048);
    juce::FloatVectorOperations::fill(buf.getWritePointer(1), 0.9f, 2048);
    juce::MidiBuffer midi;
    adsr.processBlock(buf, midi);
    EXPECT_NEAR(peakOf(buf, 0, 1024), 0.6f, 0.01f);
    EXPECT_NEAR(peakOf(buf, 1, 1024), 0.9f, 0.01f);
}

TEST_F(ADSRTest, FullGateWithVelocityOnStaysAtUnity) {
    fastEnvelope(adsr);
    setPoly(adsr, true);
    setFloat(adsr, "velocity", 100.0f);
    juce::AudioBuffer<float> buf(14, 2048);
    buf.clear();
    juce::FloatVectorOperations::fill(buf.getWritePointer(0), 1.0f, 2048);
    juce::MidiBuffer midi;
    adsr.processBlock(buf, midi);
    EXPECT_NEAR(peakOf(buf, 0, 1024), 1.0f, 0.01f);
}

TEST_F(ADSRTest, VelocityCVJackSetsTheAmount) {
    fastEnvelope(adsr);
    juce::AudioBuffer<float> buf(15, 2048);
    buf.clear();
    juce::FloatVectorOperations::fill(buf.getWritePointer(14), 1.0f, 2048); // +100 % of the 0 % knob
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)40), 0);
    adsr.processBlock(buf, midi);
    EXPECT_NEAR(peakOf(buf, 0, 1024), 40.0f / 127.0f, 0.02f);
}

TEST_F(ADSRTest, VelocityJackIsDeclaredAndBound) {
    bool found = false;
    for (const auto& t : adsr.getModulationTargets()) {
        if (t.paramId == "velocity") {
            EXPECT_EQ(t.channelIndex, 14);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// Fed by Poly MIDI: the output must scale with the note's real velocity, whatever the Threshold, block size or
// retrigger. Poly MIDI's gate rises over 5 ms, so its level where it crosses the Threshold is not the velocity.

namespace {

constexpr double kRate = 44100.0;

struct Ev {
    int at, note, vel; // absolute sample; vel 0 is a note-off
};

struct Rig {
    PolyMidiModule midi;
    ADSRModule adsr;
    int block;

    Rig(int blockSize, float threshold, bool velToGate, float velocityAmount)
        : block(blockSize) {
        midi.prepareToPlay(kRate, blockSize);
        adsr.prepareToPlay(kRate, blockSize);
        setPoly(adsr, true);
        setFloat(adsr, "attack", 0.0f);
        setFloat(adsr, "decay", 0.05f);
        setFloat(adsr, "sustain", 1.0f);
        setFloat(adsr, "gateThreshold", threshold);
        setFloat(adsr, "velocity", velocityAmount);
        auto* p = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&midi, "velToGate"));
        p->setValueNotifyingHost(velToGate ? 1.0f : 0.0f);
    }

    // Returns voice 0's envelope output over `samples` samples.
    std::vector<float> run(int samples, const std::vector<Ev>& events) {
        std::vector<float> out;
        juce::AudioBuffer<float> buf(16, block);
        for (int start = 0; start < samples; start += block) {
            buf.clear();
            juce::MidiBuffer m;
            for (const auto& e : events)
                if (e.at >= start && e.at < start + block)
                    m.addEvent(e.vel > 0 ? juce::MidiMessage::noteOn(1, e.note, (juce::uint8)e.vel)
                                         : juce::MidiMessage::noteOff(1, e.note),
                               e.at - start);
            midi.processBlock(buf, m);
            juce::AudioBuffer<float> adsrBuf(14, block);
            adsrBuf.clear();
            for (int v = 0; v < 8; ++v)
                adsrBuf.copyFrom(v, 0, buf, v + 8, 0, block);
            juce::MidiBuffer none;
            adsr.processBlock(adsrBuf, none);
            out.insert(out.end(), adsrBuf.getReadPointer(0), adsrBuf.getReadPointer(0) + block);
        }
        return out;
    }
};

float peakBetween(const std::vector<float>& v, int from, int to) {
    float p = 0.0f;
    for (int i = from; i < to && i < (int)v.size(); ++i)
        p = std::max(p, std::abs(v[(size_t)i]));
    return p;
}

} // namespace

// Regression test for FRO1373: the velocity scale was read from the gate's level where it crossed the Threshold.
TEST(ADSRPolyMidiVelocity, FullVelocityAmountScalesByTheNoteVelocityAtEveryThresholdAndBlockSize) {
    for (const int block : {64, 512})
        for (const float threshold : {0.1f, 0.5f, 0.9f})
            for (const int vel : {20, 64, 127}) {
                // The gate's height is the velocity, so a note not above the Threshold never opens it.
                if ((float)vel / 127.0f <= threshold + 0.06f)
                    continue;
                Rig rig(block, threshold, true, 100.0f);
                const auto out = rig.run(8192, {{512, 60, vel}});
                EXPECT_NEAR(peakBetween(out, 4096, 8192), (float)vel / 127.0f, 0.02f)
                    << "block " << block << " threshold " << threshold << " velocity " << vel;
            }
}

TEST(ADSRPolyMidiVelocity, FullGateIsUnityAtEveryThresholdWhenVelocityDoesNotSetTheGate) {
    for (const int block : {64, 512})
        for (const float threshold : {0.1f, 0.5f, 0.9f}) {
            Rig rig(block, threshold, false, 100.0f);
            const auto out = rig.run(8192, {{512, 60, 20}});
            EXPECT_NEAR(peakBetween(out, 4096, 8192), 1.0f, 0.02f) << "block " << block << " threshold " << threshold;
        }
}

TEST(ADSRPolyMidiVelocity, RetriggerOfAHeldNoteScalesByTheNewVelocity) {
    for (const int block : {64, 512})
        for (const float threshold : {0.1f, 0.5f}) {
            Rig rig(block, threshold, true, 100.0f);
            const auto out = rig.run(16384, {{512, 60, 127}, {8192, 60, 64}});
            EXPECT_NEAR(peakBetween(out, 4096, 8000), 1.0f, 0.02f) << "block " << block << " threshold " << threshold;
            EXPECT_NEAR(peakBetween(out, 12288, 16384), 64.0f / 127.0f, 0.02f)
                << "block " << block << " threshold " << threshold;
        }
}
