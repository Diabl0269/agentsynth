// WavetableOscillatorModuleSyncTests.cpp
// A real Oscillator cabled into a Wavetable's Restart jack gives clean oscillator sync, as it does on the Oscillator:
// the synced wave repeats once per master cycle.

#include "../SyncMeasure.h"
#include "Modules/ADSRModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "WavetableOscillatorModuleTestFixture.h"

namespace {

using WT = WavetableOscillatorModule;

constexpr double kWtSyncRate = 48000.0;
constexpr int kWtSyncBlock = 512;
constexpr int kWtSyncSkip = 9600; // past the first note's attack and decay
constexpr int kWtSyncSeconds = 48000;
constexpr float kWtNoteHz = 220.0f;
constexpr float kWtSawPosition = 2.0f / 3.0f; // Basic Shapes: sine, triangle, saw, square at thirds

struct WtSyncCase {
    int masterWave; // 0 Sine, 1 Square, 2 Saw, 3 Triangle
    int masterHz;
    int unison;
    float rand; // 0..100, the Oscillator's scale; the Wavetable takes it as 0..1
    bool poly;
};

juce::String describeWtCase(const WtSyncCase& c) {
    static const char* waves[] = {"Sine", "Square", "Saw", "Triangle"};
    return juce::String(waves[c.masterWave]) + " " + juce::String(c.masterHz) + " Hz, unison " +
           juce::String(c.unison) + ", rand " + juce::String((int)c.rand) + (c.poly ? ", poly" : ", mono");
}

void setWtActual(juce::AudioProcessor& m, const char* id, float actual) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

// A real Oscillator on a Fixed rate, its Audio L cabled into the Restart jack of a Wavetable playing a saw (in poly,
// a three-note chord with each voice restarted by the master's matching voice). The Wavetable's output passes an ADSR
// and a VCA at full sustain. Returns one second of the result, past the first note's attack. The modules are on the
// heap: a Wavetable is large and Windows gives a test thread 1 MB of stack.
std::vector<float> renderWavetableSyncedNote(const WtSyncCase& c, bool patched = true) {
    auto master = std::make_unique<OscillatorModule>();
    auto target = std::make_unique<WT>();
    auto adsr = std::make_unique<ADSRModule>();
    auto vca = std::make_unique<VCAModule>();
    setWtActual(*master, "waveform", (float)c.masterWave);
    setWtActual(*master, "rateMode", 1.0f);
    setWtActual(*master, "fixedRate", (float)c.masterHz);
    setWtActual(*target, "position", kWtSawPosition);
    setWtActual(*target, "unison", (float)c.unison);
    setWtActual(*target, "detune", c.unison > 1 ? 20.0f : 0.0f);
    setWtActual(*target, "randomPhase", c.rand * 0.01f);
    if (c.poly) {
        setWtActual(*master, "poly", 1.0f);
        setWtActual(*target, "poly", 1.0f);
    }
    setWtActual(*adsr, "attack", 0.001f);
    setWtActual(*adsr, "sustain", 1.0f);
    for (juce::AudioProcessor* p :
         {static_cast<juce::AudioProcessor*>(master.get()), static_cast<juce::AudioProcessor*>(target.get()),
          static_cast<juce::AudioProcessor*>(adsr.get()), static_cast<juce::AudioProcessor*>(vca.get())})
        p->prepareToPlay(kWtSyncRate, kWtSyncBlock);

    const float chordHz[] = {220.0f, 277.18f, 329.63f};
    const int voices = c.poly ? 3 : 1;
    std::vector<float> out;
    for (int pos = 0; pos < kWtSyncSkip + kWtSyncSeconds; pos += kWtSyncBlock) {
        juce::AudioBuffer<float> m(OscillatorModule::kNumOutputs, kWtSyncBlock), t(WT::kNumOutputs, kWtSyncBlock);
        juce::AudioBuffer<float> a(16, kWtSyncBlock), v(16, kWtSyncBlock);
        m.clear();
        t.clear();
        a.clear();
        v.clear();
        juce::MidiBuffer note, none;
        if (pos == 0)
            note.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0); // 220 Hz
        for (int i = 0; i < voices && c.poly; ++i) {
            juce::FloatVectorOperations::fill(m.getWritePointer(i), chordHz[i], kWtSyncBlock);
            juce::FloatVectorOperations::fill(t.getWritePointer(i), chordHz[i], kWtSyncBlock);
        }
        master->processBlock(m, none);
        if (patched)
            for (int i = 0; i < voices; ++i)
                t.copyFrom(c.poly ? WT::kGateBase + i : (int)WT::kJackGate, 0, m, i, 0, kWtSyncBlock);
        juce::MidiBuffer noteForTarget = note, noteForAdsr = note;
        target->processBlock(t, noteForTarget);
        adsr->processBlock(a, noteForAdsr);
        for (int i = 0; i < voices; ++i)
            v.addFrom(0, 0, t, i, 0, kWtSyncBlock, 1.0f / (float)voices);
        v.copyFrom(1, 0, a, 0, 0, kWtSyncBlock);
        vca->processBlock(v, none);
        if (pos >= kWtSyncSkip)
            out.insert(out.end(), v.getReadPointer(0), v.getReadPointer(0) + kWtSyncBlock);
    }
    out.resize((size_t)kWtSyncSeconds);
    return out;
}

} // namespace

// Regression test for FRO1358: a real Oscillator cabled into a Wavetable's Restart crackled like the Oscillator's did
// before it learned sync. Rand above 0 drew a new random phase at every edge, and the 64-sample hand-over averaged a
// fast master's wave down to a quiet remnant. A synced wave must repeat once per master cycle and keep its level.
TEST(WavetableOscillatorSync, RestartFromARealOscillator) {
    constexpr double kMaxOffHarmonicDb = -30.0;
    constexpr float kMinLevelOfFree = 0.6f;
    for (const bool poly : {false, true})
        for (const int unison : {1, 5}) {
            const auto free = renderWavetableSyncedNote({0, 100, unison, 0.0f, poly}, /*patched*/ false);
            for (const float rand : {0.0f, 50.0f, 100.0f})
                for (const int wave : {0, 1, 2, 3})
                    for (const int hz : {100, 440, 2000, 301}) {
                        const WtSyncCase c{wave, hz, unison, rand, poly};
                        const auto x = renderWavetableSyncedNote(c);
                        const double off = syncmeasure::offHarmonicDb(x, hz, syncmeasure::acEnergy(free), kWtSyncRate);
                        EXPECT_LE(off, kMaxOffHarmonicDb) << describeWtCase(c) << ": " << off << " dB";
                        // With Rand, a short cycle of a slow wave can sit near its zero, so only Rand 0 has a floor.
                        // The table's saw starts at zero and climbs 2 per cycle, so a master faster than twice the
                        // note only lets it climb to 2 * note / master of its height (the Oscillator's saw starts
                        // at its trough, so its peak does not shrink): the floor is that share of the free peak.
                        if (rand == 0.0f) {
                            const float reachable = std::min(1.0f, 2.0f * kWtNoteHz / (float)hz);
                            EXPECT_GE(syncmeasure::peakOf(x), kMinLevelOfFree * reachable * syncmeasure::peakOf(free))
                                << describeWtCase(c);
                        }
                    }
        }
}
