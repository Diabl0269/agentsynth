// A unison stack is as loud at Rand 0 as at any other Rand: the first 200 ms of a note, rendered at Rand 0, 50 and
// 100 with Unison 1, 3 and 7, agree within 1 dB for the Oscillator and the Wavetable. At Rand 0 every voice starts in
// phase and adds coherently, which the constant-power divisor (built for uncorrelated voices) does not account for.
// Also holds the top of the spectrum (5 kHz and up) of the same note at Rand 0 to that of Rand 100.

#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"

#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;
constexpr int kNotes = 12;       // Rand > 0 draws a new spread per note: average over several
constexpr int kNoteBlocks = 48;  // 0.56 s per note, note-off after 0.35 s
constexpr size_t kWindow = 8820; // the first 200 ms
constexpr size_t kFftSize = 8192;

void setActual(juce::AudioProcessor& m, const char* id, float actual) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&m, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(actual));
}

struct Window {
    double power = 0.0;     ///< mean square over both legs, the first 200 ms, mean over the notes
    double bandPower = 0.0; ///< power at 5 kHz and up in the same window (Hann FFT of Audio L), mean over the notes
};

double db(double power) { return 10.0 * std::log10(std::max(power, 1e-18)); }

// `randFull` is the module's own Rand value for 100 %; `configure` sets the oscillator-specific parameters.
template <typename Module, typename Configure>
Window measureStart(int unison, float randFraction, float randFull, int note, Configure configure) {
    auto m = std::make_unique<Module>();
    setActual(*m, "level", 1.0f);
    setActual(*m, "unison", (float)unison);
    setActual(*m, "randomPhase", randFraction * randFull);
    configure(*m);
    m->prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> buf(Module::kNumOutputs, kBlock);
    Window w;
    for (int n = 0; n < kNotes; ++n) {
        std::vector<float> left, right;
        for (int b = 0; b < kNoteBlocks; ++b) {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)100), 0);
            if (b == 34)
                midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
            m->processBlock(buf, midi);
            left.insert(left.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
            right.insert(right.end(), buf.getReadPointer(Module::kRightBase),
                         buf.getReadPointer(Module::kRightBase) + kBlock);
        }
        double e = 0.0;
        for (size_t i = 0; i < kWindow; ++i)
            e += 0.5 * ((double)left[i] * left[i] + (double)right[i] * right[i]);
        w.power += e / (double)kWindow / kNotes;

        std::vector<float> fft(2 * kFftSize, 0.0f);
        for (size_t i = 0; i < kFftSize; ++i)
            fft[i] = left[i] * (float)(0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * (double)i /
                                                            (double)(kFftSize - 1)));
        juce::dsp::FFT(13).performRealOnlyForwardTransform(fft.data());
        double band = 0.0;
        for (size_t bin = 1; bin < kFftSize / 2; ++bin)
            if ((double)bin * kRate / (double)kFftSize >= 5000.0)
                band += (double)fft[2 * bin] * fft[2 * bin] + (double)fft[2 * bin + 1] * fft[2 * bin + 1];
        w.bandPower += band / kNotes;
    }
    return w;
}

Window oscillatorStart(int unison, float rand, int note = 57) {
    return measureStart<OscillatorModule>(unison, rand, 100.0f, note, [](OscillatorModule& m) {
        setActual(m, "waveform", 2.0f); // Saw, 220 Hz: the hottest waveform
    });
}

Window wavetableStart(int unison, float rand, int note = 57) {
    return measureStart<WavetableOscillatorModule>(unison, rand, 1.0f, note, [](WavetableOscillatorModule& m) {
        setActual(m, "position", 0.67f); // a saw frame
    });
}

} // namespace

// Regression test for FRO1423: at Rand 0 a stack of 3 or 7 voices opened several dB louder than at Rand 50 or 100,
// because the voices start in phase and add coherently.
TEST(UnisonRandLoudness, TheFirstTwoHundredMsAreAsLoudAtEveryRand) {
    for (const int note : {36, 57, 84}) {
        for (const bool wavetable : {false, true}) {
            for (const int unison : {1, 3, 7}) {
                auto level = [&](float rand) {
                    return db(
                        (wavetable ? wavetableStart(unison, rand, note) : oscillatorStart(unison, rand, note)).power);
                };
                const double r0 = level(0.0f), r50 = level(0.5f), r100 = level(1.0f);
                const char* name = wavetable ? "Wavetable" : "Oscillator";
                std::printf("[measurement] %s note %d unison %d first 200 ms: Rand 0 %.2f, 50 %.2f, 100 %.2f dB\n",
                            name, note, unison, r0, r50, r100);
                EXPECT_NEAR(r0, r100, 1.0) << name << " note " << note << " unison " << unison << " Rand 0";
                EXPECT_NEAR(r50, r100, 1.0) << name << " note " << note << " unison " << unison << " Rand 50";
            }
        }
    }
}

// Regression test for FRO1425: Unison 7 at Detune 35, Rand 0 sounded brighter than the reference. The level at
// 5 kHz and up in the first 200 ms must match Rand 100 within 1 dB.
TEST(UnisonRandLoudness, TheTopOfTheSpectrumIsTheSameAtRandZeroAndRandOneHundred) {
    for (const int note : {36, 57, 84}) {
        for (const int unison : {3, 7}) {
            const double r0 = db(oscillatorStart(unison, 0.0f, note).bandPower);
            const double r100 = db(oscillatorStart(unison, 1.0f, note).bandPower);
            std::printf("[measurement] Oscillator note %d unison %d >=5 kHz: Rand 0 %.2f dB, Rand 100 %.2f dB\n", note,
                        unison, r0, r100);
            EXPECT_NEAR(r0, r100, 1.0) << "note " << note << " unison " << unison;
        }
    }
}
