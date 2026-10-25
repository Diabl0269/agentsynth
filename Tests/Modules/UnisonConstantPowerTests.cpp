// A unison stack is constant power: the Oscillator and the Wavetable divide it by the root of the summed
// squared Blend gains, so its loudness stays that of one voice at any Unison count. Also pins the level of
// the two oscillators against each other (a documented measurement, not a requirement).

#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UnisonStackMeasure.h"

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

// The reference stack's numbers: a Saw at 220 Hz held with Rand 100 %, a 34 cent detune, measured as the share of
// the mid power on the centre voice, the side/mid level ratio and the loudness against one voice. They come from
// a reference synth measured as a black box (Blend and Width as 0..1 here, 0..100 % there).
namespace {
struct Reference {
    int unison;
    float blend;
    float width;
    double centreShare; ///< 0 where the row is not about Blend
    double sideMid;     ///< 0 where the row is not about Width
};

const Reference kReference[] = {
    {7, 0.0f, 0.0f, 0.988, 0.0},  {7, 0.25f, 0.0f, 0.9125, 0.0}, {7, 0.5f, 0.0f, 0.547, 0.0},
    {7, 0.75f, 0.0f, 0.216, 0.0}, {7, 1.0f, 0.0f, 0.086, 0.0},   {3, 0.5f, 0.0f, 0.784, 0.0},
    {3, 1.0f, 0.0f, 0.220, 0.0},  {5, 1.0f, 0.0f, 0.1239, 0.0},  {7, 1.0f, 0.5f, 0.0, 0.565},
    {7, 1.0f, 1.0f, 0.0, 0.966},  {7, 0.5f, 0.5f, 0.0, 0.409},   {7, 0.5f, 1.0f, 0.0, 0.748},
    {7, 0.25f, 1.0f, 0.0, 0.362}, {5, 1.0f, 1.0f, 0.0, 0.953},   {3, 1.0f, 1.0f, 0.0, 0.923},
    {3, 0.5f, 1.0f, 0.0, 0.554},  {5, 0.5f, 0.5f, 0.0, 0.366},
};
} // namespace

// Regression test for FRO1077: Blend scaled only the outer voices between 1 and `blend` and Width used a
// balance law, so both moved the sound far less than the reference (Blend 100 % left 14 % on the centre voice
// of 7 instead of 9 %, Width 100 % reached a side/mid ratio of 0.45 instead of 0.97).
TEST(UnisonReferenceTable, BlendAndWidthLandOnTheReferenceNumbers) {
    const auto one = unison_measure::measureStack(1, 0.0f, 0.0f);
    for (const auto& r : kReference) {
        const auto s = unison_measure::measureStack(r.unison, r.blend, r.width);
        if (r.centreShare > 0.0)
            EXPECT_NEAR(s.centreShare, r.centreShare, 0.04)
                << r.unison << " voices Blend " << r.blend << " centre share";
        if (r.sideMid > 0.0)
            EXPECT_NEAR(s.sideMid, r.sideMid, 0.2 * r.sideMid)
                << r.unison << " voices Blend " << r.blend << " Width " << r.width << " side/mid";
        EXPECT_NEAR(20.0 * std::log10(s.rms / one.rms), 0.0, 1.5)
            << r.unison << " voices Blend " << r.blend << " Width " << r.width << " loudness";
    }
}

namespace {
// Largest sample-to-sample step on either leg while Unison changes from `from` to `to` in a held note.
float largestStepAcrossUnisonChange(int from, int to, float width, bool poly, int note) {
    OscillatorModule m;
    setActual(m, "level", 1.0f);
    setActual(m, "waveform", 0.0f);
    setActual(m, "unison", (float)from);
    setActual(m, "detune", 30.0f);
    setActual(m, "width", width);
    setActual(m, "blend", 1.0f);
    setActual(m, "randomPhase", 100.0f);
    if (poly)
        setActual(m, "poly", 1.0f);
    m.prepareToPlay(kRate, kBlock);
    float worst = 0.0f;
    float lastL = 0.0f, lastR = 0.0f;
    for (int b = 0; b < 12; ++b) {
        if (b == 6)
            setActual(m, "unison", (float)to);
        juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kBlock);
        buf.clear();
        juce::MidiBuffer midi;
        if (poly)
            juce::FloatVectorOperations::fill(buf.getWritePointer(0),
                                              440.0f * std::pow(2.0f, (float)(note - 69) / 12.0f), kBlock);
        else if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)100), 0);
        m.processBlock(buf, midi);
        for (int i = 0; i < kBlock; ++i) {
            const float l = buf.getReadPointer(0)[i];
            const float r = buf.getReadPointer(OscillatorModule::kRightBase)[i];
            if (b >= 2) // past the note start
                worst = std::max({worst, std::abs(l - lastL), std::abs(r - lastR)});
            lastL = l;
            lastR = r;
        }
    }
    return worst;
}
} // namespace

namespace {
// The same for the Wavetable (a sine, Unison from `from` to `to` after six blocks of a held note).
float largestWavetableStepAcrossUnisonChange(int from, int to, float width, bool poly, int note) {
    WavetableOscillatorModule m;
    setActual(m, "level", 1.0f);
    setActual(m, "position", 0.0f);
    setActual(m, "unison", (float)from);
    setActual(m, "detune", 30.0f);
    setActual(m, "width", width);
    setActual(m, "blend", 1.0f);
    setActual(m, "randomPhase", 1.0f);
    if (poly)
        setActual(m, "poly", 1.0f);
    m.prepareToPlay(kRate, kBlock);
    float worst = 0.0f;
    float lastL = 0.0f, lastR = 0.0f;
    for (int b = 0; b < 12; ++b) {
        if (b == 6)
            setActual(m, "unison", (float)to);
        juce::AudioBuffer<float> buf(WavetableOscillatorModule::kNumOutputs, kBlock);
        buf.clear();
        juce::MidiBuffer midi;
        if (poly)
            juce::FloatVectorOperations::fill(buf.getWritePointer(0),
                                              440.0f * std::pow(2.0f, (float)(note - 69) / 12.0f), kBlock);
        else if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)100), 0);
        m.processBlock(buf, midi);
        for (int i = 0; i < kBlock; ++i) {
            const float l = buf.getReadPointer(0)[i];
            const float r = buf.getReadPointer(WavetableOscillatorModule::kRightBase)[i];
            if (b >= 2)
                worst = std::max({worst, std::abs(l - lastL), std::abs(r - lastR)});
            lastL = l;
            lastR = r;
        }
    }
    return worst;
}
} // namespace

// Regression test for FRO1129: the Wavetable changed its voice count instantly, so a held stack stepped.
TEST(UnisonReferenceTable, ChangingUnisonInAHeldWavetableNoteHasNoStep) {
    for (const bool poly : {false, true})
        for (const float width : {0.0f, 1.0f})
            for (const auto& [from, to] : {std::pair{1, 7}, std::pair{7, 1}, std::pair{3, 4}, std::pair{5, 8}})
                for (int note = 45; note < 58; note += 2)
                    EXPECT_LT(largestWavetableStepAcrossUnisonChange(from, to, width, poly, note), 0.1f)
                        << from << " -> " << to << " width " << width << (poly ? " poly" : " mono") << " note " << note;
}

// Regression test for FRO1077: the voice count changed instantly, so a held stack stepped by up to the whole
// signal level at the block where Unison changed.
TEST(UnisonReferenceTable, ChangingUnisonInAHeldNoteHasNoStep) {
    // A sine up to 220 Hz moves at most 0.032 per sample; a stack of a few voices roughly doubles that and the
    // fades add a little, while an instant change steps by up to the signal level.
    for (const bool poly : {false, true})
        for (const float width : {0.0f, 1.0f})
            for (const auto& [from, to] : {std::pair{1, 7}, std::pair{7, 1}, std::pair{3, 4}, std::pair{5, 8}})
                for (int note = 45; note < 58; note += 2) // the phase at the change differs with the pitch
                    EXPECT_LT(largestStepAcrossUnisonChange(from, to, width, poly, note), 0.1f)
                        << from << " -> " << to << " width " << width << (poly ? " poly" : " mono") << " note " << note;
}
