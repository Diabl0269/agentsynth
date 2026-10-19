// A unison stack is constant power: the Oscillator and the Wavetable divide it by the root of the summed
// squared Blend gains, so its loudness stays that of one voice at any Unison count. Also pins the level of
// the two oscillators against each other (a documented measurement, not a requirement).

#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"

#include <cmath>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

void setActual(juce::AudioProcessor& m, const char* id, float actual) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

struct Level {
    double rmsDb = 0.0;
    double peakDb = 0.0;
};

double toDb(double x) { return 20.0 * std::log10(std::max(x, 1e-9)); }

// Renders `seconds` of note 48 (C3) on Audio L after one settling block and returns its RMS and peak.
template <typename Module>
Level measure(Module& m, double seconds = 1.0) {
    m.prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> buf(Module::kNumOutputs, kBlock);
    double sumSq = 0.0, peak = 0.0;
    long count = 0;
    const int blocks = (int)std::ceil(seconds * kRate / kBlock);
    for (int b = 0; b <= blocks; ++b) {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8)100), 0);
        m.processBlock(buf, midi);
        if (b == 0)
            continue;
        const float* l = buf.getReadPointer(0);
        for (int i = 0; i < kBlock; ++i) {
            sumSq += (double)l[i] * l[i];
            peak = std::max(peak, (double)std::fabs(l[i]));
        }
        count += kBlock;
    }
    return {toDb(std::sqrt(sumSq / (double)count)), toDb(peak)};
}

Level oscillatorStack(int unison, float blend = 1.0f) {
    OscillatorModule m;
    setActual(m, "level", 1.0f);
    setActual(m, "waveform", 0.0f);
    setActual(m, "unison", (float)unison);
    setActual(m, "detune", 30.0f);
    setActual(m, "blend", blend);
    setActual(m, "randomPhase", 100.0f);
    return measure(m);
}

Level wavetableStack(int unison, float blend = 1.0f) {
    WavetableOscillatorModule m;
    setActual(m, "level", 1.0f);
    setActual(m, "unison", (float)unison);
    setActual(m, "detune", 30.0f);
    setActual(m, "blend", blend);
    setActual(m, "randomPhase", 1.0f);
    return measure(m);
}

} // namespace

// Regression test for FRO848: the stack was divided by the sum of the gains, so 2/4/8 uncorrelated voices lost
// about 3/6/9 dB against one voice.
TEST(UnisonConstantPower, OscillatorStackKeepsTheLoudnessOfOneVoice) {
    const Level one = oscillatorStack(1);
    for (const int n : {2, 4, 8})
        EXPECT_NEAR(oscillatorStack(n).rmsDb, one.rmsDb, 1.5) << n << " voices";
}

TEST(UnisonConstantPower, WavetableStackKeepsTheLoudnessOfOneVoice) {
    const Level one = wavetableStack(1);
    for (const int n : {2, 4, 8})
        EXPECT_NEAR(wavetableStack(n).rmsDb, one.rmsDb, 1.5) << n << " voices";
}

TEST(UnisonConstantPower, BlendZeroLeavesTheCentreVoiceAtTheSingleVoiceLevel) {
    // Three voices at Blend 0 leave only the middle one, divided by exactly 1.
    EXPECT_NEAR(oscillatorStack(3, 0.0f).rmsDb, oscillatorStack(1).rmsDb, 0.1);
    EXPECT_NEAR(wavetableStack(3, 0.0f).rmsDb, wavetableStack(1).rmsDb, 0.1);
}

// Documented measurement: C3, level 1, unison 1, mono. The Wavetable's tables are peak-normalised, so a saw
// frame sits below the Oscillator's naive saw; the numbers are printed so a change shows in the test log.
TEST(UnisonConstantPower, OscillatorAndWavetableLevelsAtUnisonOne) {
    // On the heap: four oscillator modules at once overflow the 1 MB default stack on Windows.
    auto oscSine = std::make_unique<OscillatorModule>();
    setActual(*oscSine, "level", 1.0f);
    setActual(*oscSine, "waveform", 0.0f);
    auto wtSine = std::make_unique<WavetableOscillatorModule>();
    setActual(*wtSine, "level", 1.0f);
    setActual(*wtSine, "position", 0.0f);

    auto oscSaw = std::make_unique<OscillatorModule>();
    setActual(*oscSaw, "level", 1.0f);
    setActual(*oscSaw, "waveform", 2.0f);
    auto wtSaw = std::make_unique<WavetableOscillatorModule>();
    setActual(*wtSaw, "level", 1.0f);
    setActual(*wtSaw, "position", 0.67f);

    const Level a = measure(*oscSine), b = measure(*wtSine), c = measure(*oscSaw), d = measure(*wtSaw);
    std::printf("[measurement] sine  Oscillator rms %.2f peak %.2f dBFS | Wavetable rms %.2f peak %.2f | diff rms %.2f "
                "peak %.2f\n",
                a.rmsDb, a.peakDb, b.rmsDb, b.peakDb, b.rmsDb - a.rmsDb, b.peakDb - a.peakDb);
    std::printf("[measurement] saw   Oscillator rms %.2f peak %.2f dBFS | Wavetable rms %.2f peak %.2f | diff rms %.2f "
                "peak %.2f\n",
                c.rmsDb, c.peakDb, d.rmsDb, d.peakDb, d.rmsDb - c.rmsDb, d.peakDb - c.peakDb);
    EXPECT_LE(a.peakDb, 0.5);
    EXPECT_LE(b.peakDb, 0.5);
    EXPECT_LE(c.peakDb, 0.5);
    EXPECT_LE(d.peakDb, 0.5);
}
