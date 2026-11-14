// Velocity reaching the sound: renders Oscillator -> VCA with a Poly MIDI -> ADSR envelope on the VCA's
// gain jacks (the chain of a default poly patch) and measures the RMS of a soft and a hard note.

#include "ADSRTestFixture.h"
#include "Modules/OscillatorModule.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/VCAModule.h"
#include <cmath>

namespace {

constexpr double kRenderRate = 44100.0;
constexpr int kBlock = 256;
constexpr int kNoteLen = 16384;

void setParam(ModuleBase& m, const juce::String& id, float actual) {
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

void setBool(ModuleBase& m, const juce::String& id, bool on) {
    auto* p = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(on ? 1.0f : 0.0f);
}

// RMS of the last half of a note of `velocity` (0..127) held for kNoteLen samples.
float noteRms(bool poly, float velocityAmount, bool velToGate, int velocity) {
    OscillatorModule osc;
    PolyMidiModule polyMidi;
    ADSRModule adsr;
    VCAModule vca;
    for (ModuleBase* m : std::initializer_list<ModuleBase*>{&osc, &polyMidi, &adsr, &vca})
        m->prepareToPlay(kRenderRate, kBlock);
    setBool(osc, "poly", poly);
    setBool(vca, "poly", poly);
    setBool(adsr, "poly", poly);
    setParam(adsr, "attack", 0.001f);
    setParam(adsr, "sustain", 1.0f);
    setParam(adsr, "gateThreshold", 0.001f);
    setParam(adsr, "velocity", velocityAmount);
    setBool(polyMidi, "velToGate", velToGate);

    double sumSq = 0.0;
    int counted = 0;
    for (int start = 0; start < kNoteLen; start += kBlock) {
        juce::MidiBuffer midi;
        if (start == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)velocity), 0);

        juce::AudioBuffer<float> oscBuf(24, kBlock);
        oscBuf.clear();
        juce::MidiBuffer oscMidi(midi);
        osc.processBlock(oscBuf, oscMidi);

        // The ADSR's gate: Poly MIDI's per-voice gate in poly, the note itself in mono.
        juce::AudioBuffer<float> envBuf(14, kBlock);
        envBuf.clear();
        juce::MidiBuffer envMidi;
        if (poly) {
            juce::AudioBuffer<float> gateBuf(16, kBlock);
            gateBuf.clear();
            juce::MidiBuffer pm(midi);
            polyMidi.processBlock(gateBuf, pm);
            for (int v = 0; v < 8; ++v)
                envBuf.copyFrom(v, 0, gateBuf, v + 8, 0, kBlock);
        } else {
            envMidi = midi;
        }
        adsr.processBlock(envBuf, envMidi);

        juce::AudioBuffer<float> vcaBuf(24, kBlock);
        vcaBuf.clear();
        const int voices = poly ? 8 : 1;
        for (int v = 0; v < voices; ++v)
            vcaBuf.copyFrom(v, 0, oscBuf, v, 0, kBlock);
        if (poly)
            for (int v = 0; v < 8; ++v)
                vcaBuf.copyFrom(8 + v, 0, envBuf, v, 0, kBlock);
        else
            vcaBuf.copyFrom(1, 0, envBuf, 0, 0, kBlock);
        juce::MidiBuffer none;
        vca.processBlock(vcaBuf, none);
        if (start >= kNoteLen / 2)
            for (int i = 0; i < kBlock; ++i) {
                sumSq += (double)vcaBuf.getSample(0, i) * vcaBuf.getSample(0, i);
                ++counted;
            }
    }
    return (float)std::sqrt(sumSq / counted);
}

} // namespace

// Regression test for FRO1426: a poly ADSR at Velocity 100 % played a soft and a hard note equally loud
// because Poly MIDI's "Vel -> Gate" was off (its default), so every gate rose to 1.0 and carried no velocity.
TEST(ADSRVelocityRender, VelocityShapesLoudnessOnlyInPolyWithVelocityAboveZeroAndVelocityToGateOn) {
    for (const bool poly : {false, true})
        for (const float amount : {0.0f, 50.0f, 100.0f})
            for (const bool velToGate : {false, true}) {
                const float soft = noteRms(poly, amount, velToGate, 1);
                const float hard = noteRms(poly, amount, velToGate, 127);
                ASSERT_GT(hard, 0.01f);
                const std::string what = std::string(poly ? "poly" : "mono") + " velocity " +
                                         std::to_string((int)amount) + (velToGate ? " vel->gate on" : " vel->gate off");
                // Mono reads the note's own velocity, so only the amount matters; poly reads the gate's level.
                const bool shaped = amount > 0.0f && (poly ? velToGate : true);
                std::cout << what << ": soft rms " << soft << ", hard rms " << hard << "\n";
                if (shaped)
                    EXPECT_LT(soft, hard * 0.8f) << what << " soft " << soft << " hard " << hard;
                else
                    EXPECT_NEAR(soft, hard, hard * 0.02f) << what << " soft " << soft << " hard " << hard;
            }
}
