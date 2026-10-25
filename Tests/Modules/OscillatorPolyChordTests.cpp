// A poly Oscillator chord through the default instrument chain (Poly MIDI -> Oscillator -> ADSR -> VCA): the
// sum of the held notes reaches the output as the sum of their sines, not as a distorted version of it, and
// grows with the number of notes.

#include "../Engine/AudioRendering/AudioRenderingTestHelpers.h"
#include "Modules/ADSRModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/SoftLimit.h"
#include "Modules/VCAModule.h"

#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

struct Voice {
    float phase = 273.7f;
    float rand = 0.0f;
    int unison = 1;
    int waveform = 0;
    float blend = 0.75f;
};

struct Chain {
    PolyMidiModule midi;
    OscillatorModule osc;
    ADSRModule adsr;
    VCAModule vca;

    Chain(const Voice& voice, int block) {
        setParamValue(&osc, "poly", 1.0f);
        setParamValue(&osc, "phase", voice.phase);
        setParamValue(&osc, "randomPhase", voice.rand);
        setParamValue(&osc, "unison", (float)voice.unison);
        setParamValue(&osc, "waveform", (float)voice.waveform);
        setParamValue(&osc, "blend", voice.blend);
        setParamValue(&osc, "detune", 25.0f);
        setParamValue(&adsr, "poly", 1.0f);
        setParamValue(&adsr, "attack", 0.001f);
        setParamValue(&adsr, "sustain", 1.0f);
        setParamValue(&adsr, "release", 0.015f);
        setParamValue(&vca, "poly", 1.0f);
        setParamValue(&vca, "gain", 0.5f);
        for (juce::AudioProcessor* p :
             {static_cast<juce::AudioProcessor*>(&midi), static_cast<juce::AudioProcessor*>(&osc),
              static_cast<juce::AudioProcessor*>(&adsr), static_cast<juce::AudioProcessor*>(&vca)})
            p->prepareToPlay(kRate, block);
    }

    // One block: the MIDI goes to Poly MIDI and the Oscillator, whose pitch and gate fans come from Poly MIDI.
    void process(const juce::MidiBuffer& in, int n, float* out) {
        juce::AudioBuffer<float> pm(16, n);
        pm.clear();
        juce::MidiBuffer midiCopy(in);
        midi.processBlock(pm, midiCopy);

        juce::AudioBuffer<float> o(OscillatorModule::kNumOutputs, n);
        o.clear();
        for (int v = 0; v < 8; ++v) {
            o.copyFrom(v, 0, pm, v, 0, n);
            o.copyFrom(OscillatorModule::kGateBase + v, 0, pm, 8 + v, 0, n);
        }
        juce::MidiBuffer oscMidi(in);
        osc.processBlock(o, oscMidi);

        juce::AudioBuffer<float> env(16, n);
        env.clear();
        for (int v = 0; v < 8; ++v)
            env.copyFrom(v, 0, pm, 8 + v, 0, n);
        juce::MidiBuffer none;
        adsr.processBlock(env, none);

        juce::AudioBuffer<float> mix(16, n);
        mix.clear();
        for (int v = 0; v < 8; ++v) {
            mix.copyFrom(v, 0, o, v, 0, n);
            mix.copyFrom(8 + v, 0, env, v, 0, n);
        }
        vca.processBlock(mix, none);
        std::copy(mix.getReadPointer(0), mix.getReadPointer(0) + n, out);
    }
};

struct Pass {
    int onAt;
    int offAt;
    std::vector<float> out;
};

// `passes` loops of 2 s (4 beats at 120 bpm): the chord on at beat 1 for a beat and again at beat 3, the way
// the timeline writes it, in blocks of `block` samples that start `lead` samples into the first pass.
std::vector<Pass> renderChord(const std::vector<int>& notes, int passes, int block, int lead, const Voice& voice) {
    Chain chain(voice, block);
    const int loop = (int)(2.0 * kRate);
    const int total = lead + passes * loop + (int)kRate;
    std::vector<float> out((size_t)total, 0.0f);
    std::vector<Pass> found;
    for (int pass = 0; pass < passes; ++pass)
        for (const double beat : {1.0, 3.0})
            found.push_back({lead + pass * loop + (int)(beat * 0.5 * kRate),
                             lead + pass * loop + (int)((beat + 1.0) * 0.5 * kRate),
                             {}});
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min(block, total - pos);
        juce::MidiBuffer midi;
        for (const auto& f : found)
            if (f.offAt >= pos && f.offAt < pos + n)
                for (const int note : notes)
                    midi.addEvent(juce::MidiMessage::noteOff(1, note), f.offAt - pos);
        for (const auto& f : found)
            if (f.onAt >= pos && f.onAt < pos + n)
                for (const int note : notes)
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)100), f.onAt - pos);
        chain.process(midi, n, out.data() + pos);
    }
    for (auto& f : found)
        f.out = out;
    return found;
}

double hzOfNote(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

struct Fit {
    double rms = 0.0;      ///< of the window
    double residual = 0.0; ///< rms left after removing the best-fitting sine at each note
    double peak = 0.0;
};

// Least-squares fit of a sine and a cosine at each note's frequency over [from, to).
Fit fitNotes(const std::vector<float>& x, int from, int to, const std::vector<int>& notes) {
    const int k = (int)notes.size() * 2;
    std::vector<std::vector<double>> a((size_t)k, std::vector<double>((size_t)k + 1, 0.0));
    std::vector<double> basis((size_t)k);
    for (int i = from; i < to; ++i) {
        for (size_t j = 0; j < notes.size(); ++j) {
            const double w = 2.0 * kPi * hzOfNote(notes[j]) * i / kRate;
            basis[2 * j] = std::sin(w);
            basis[2 * j + 1] = std::cos(w);
        }
        for (int r = 0; r < k; ++r) {
            for (int c = 0; c < k; ++c)
                a[(size_t)r][(size_t)c] += basis[(size_t)r] * basis[(size_t)c];
            a[(size_t)r][(size_t)k] += basis[(size_t)r] * x[(size_t)i];
        }
    }
    for (int col = 0; col < k; ++col) {
        int pivot = col;
        for (int r = col + 1; r < k; ++r)
            if (std::abs(a[(size_t)r][(size_t)col]) > std::abs(a[(size_t)pivot][(size_t)col]))
                pivot = r;
        std::swap(a[(size_t)col], a[(size_t)pivot]);
        for (int r = 0; r < k; ++r) {
            if (r == col)
                continue;
            const double f = a[(size_t)r][(size_t)col] / a[(size_t)col][(size_t)col];
            for (int c = col; c <= k; ++c)
                a[(size_t)r][(size_t)c] -= f * a[(size_t)col][(size_t)c];
        }
    }
    Fit fit;
    double sumSq = 0.0, resSq = 0.0;
    for (int i = from; i < to; ++i) {
        double fitted = 0.0;
        for (size_t j = 0; j < notes.size(); ++j) {
            const double w = 2.0 * kPi * hzOfNote(notes[j]) * i / kRate;
            fitted += a[2 * j][(size_t)k] / a[2 * j][2 * j] * std::sin(w) +
                      a[2 * j + 1][(size_t)k] / a[2 * j + 1][2 * j + 1] * std::cos(w);
        }
        sumSq += (double)x[(size_t)i] * x[(size_t)i];
        resSq += ((double)x[(size_t)i] - fitted) * ((double)x[(size_t)i] - fitted);
        fit.peak = std::max(fit.peak, (double)std::abs(x[(size_t)i]));
    }
    fit.rms = std::sqrt(sumSq / (to - from));
    fit.residual = std::sqrt(resSq / (to - from));
    return fit;
}

} // namespace

// Regression test for FRO1076: a chord of three sines at the default level summed to 1.5 and the VCA's soft
// limiter clipped it, which sounds like a broken telephone: 12 % of the level was not in the notes at all. Each voice
// is now summed at a fixed lower gain, so a steady three-note chord is the sum of its sines (under 1.5 % distortion)
// at every block size and loop offset.
TEST(OscillatorPolyChord, ThreeNoteChordStaysTheSumOfItsSines) {
    for (const int block : {64, 128, 256, 441, 480, 512})
        for (const int lead : {0, 100, 333}) {
            const auto passes = renderChord({62, 66, 70}, 4, block, lead, Voice{});
            for (const auto& p : passes) {
                const auto fit = fitNotes(p.out, p.onAt + 4800, p.offAt - 480, {62, 66, 70});
                EXPECT_LT(fit.residual / fit.rms, 0.015) << "block " << block << ", lead " << lead;
            }
        }
}

// Voices add at a fixed gain, so every note makes the chord louder, and the soft limiter still caps the sum.
TEST(OscillatorPolyChord, ChordsGrowLouderWithEveryNoteAndNeverPassTheCeiling) {
    const std::vector<std::vector<int>> chords = {{60}, {62, 66, 70}, {48, 52, 55, 60, 64, 67, 72, 76}};
    for (const int block : {64, 512}) {
        double level[3] = {};
        for (size_t c = 0; c < chords.size(); ++c) {
            const auto p = renderChord(chords[c], 1, block, 0, Voice{})[0];
            const auto fit = fitNotes(p.out, p.onAt + 4800, p.offAt - 480, chords[c]);
            level[c] = fit.rms;
            EXPECT_LE(fit.peak, 0.98) << chords[c].size() << " notes, block " << block;
        }
        EXPECT_GT(level[1], level[0] * std::pow(10.0, 3.0 / 20.0)) << "three notes are at least 3 dB above one";
        EXPECT_GT(level[2], level[1]) << "and eight are louder than three";
    }
}

// Each voice's fixed gain scales one note by kPolyVoiceGain against the same note through the mono path.
TEST(OscillatorPolyChord, OneNoteIsTheVoiceGainTimesTheVcaGain) {
    const auto passes = renderChord({60}, 1, 512, 0, Voice{});
    const auto fit = fitNotes(passes[0].out, passes[0].onAt + 4800, passes[0].offAt - 480, {60});
    EXPECT_NEAR(fit.peak, 0.5 * synth::kPolyVoiceGain, 0.005);
}

namespace {

struct StartStats {
    double early = 0.0, late = 0.0, spread = 0.0, repeatDifference = 0.0;
};

// The first 100 ms of each repeat of a 3-note saw chord through the chain (Poly MIDI gate wired): the level of the
// first 30 ms against 30-100 ms, the spread of the 100 ms level over the repeats and how far two repeats differ.
StartStats startOf(const Voice& voice, std::vector<float>* firstRepeat = nullptr) {
    const auto passes = renderChord({57, 61, 64}, 4, 512, 100, voice);
    StartStats st;
    std::vector<double> total;
    auto rms = [&](const Pass& p, int a, int b) {
        double e = 0.0;
        for (int i = a; i < b; ++i)
            e += (double)p.out[(size_t)(p.onAt + i)] * p.out[(size_t)(p.onAt + i)];
        return std::sqrt(e / (b - a));
    };
    for (const auto& p : passes) {
        st.early += rms(p, 64, 1440) / (double)passes.size();
        st.late += rms(p, 1440, 4800) / (double)passes.size();
        total.push_back(rms(p, 0, 4800));
    }
    double mean = 0.0, var = 0.0;
    for (const double t : total)
        mean += t / (double)total.size();
    for (const double t : total)
        var += (t - mean) * (t - mean) / (double)total.size();
    st.spread = std::sqrt(var) / mean;
    double diff = 0.0, peak = 1e-9;
    for (int i = 0; i < 1440; ++i) {
        diff = std::max(diff, (double)std::abs(passes[0].out[(size_t)(passes[0].onAt + i)] -
                                               passes[1].out[(size_t)(passes[1].onAt + i)]));
        peak = std::max(peak, (double)std::abs(passes[0].out[(size_t)(passes[0].onAt + i)]));
    }
    st.repeatDifference = diff / peak;
    if (firstRepeat != nullptr)
        firstRepeat->assign(passes[0].out.begin() + passes[0].onAt, passes[0].out.begin() + passes[0].onAt + 1440);
    return st;
}

} // namespace

// Poly follows the same Phase and Rand feel as mono once its Gate is wired (Poly MIDI does that): Phase moves the
// attack waveform, Rand 0 repeats the chord exactly with a loud in-phase start, Rand 100 differs on every repeat.
TEST(OscillatorPolyChord, PhaseAndRandAreAudibleOnEveryRepeatOfAChord) {
    Voice base;
    base.unison = 5;
    base.waveform = 2;
    base.phase = 0.0f;
    std::vector<float> zero, quarter;
    const auto together = startOf(base, &zero);
    Voice moved = base;
    moved.phase = 90.0f;
    startOf(moved, &quarter);
    double diff = 0.0, level = 0.0;
    for (size_t i = 64; i < zero.size(); ++i) {
        diff += (double)(zero[i] - quarter[i]) * (zero[i] - quarter[i]);
        level += (double)zero[i] * zero[i];
    }
    EXPECT_GT(std::sqrt(diff / level), 0.5) << "Phase moves the attack";
    EXPECT_GT(together.early / together.late, 1.2) << "an in-phase stack starts loud";
    EXPECT_LT(together.spread, 0.05);

    Voice random = base;
    random.rand = 100.0f;
    const auto apart = startOf(random);
    EXPECT_LT(apart.early / apart.late, together.early / together.late);
    EXPECT_GT(apart.spread, 0.025) << "the stack's thickness differs from repeat to repeat";
    EXPECT_GT(apart.repeatDifference, 0.5) << "the chord starts differently each time";
}

// A chord that starts mid-block must not step the output where a block ends: the largest step is a sine's own
// slope and the loudest peak does not depend on the block size.
TEST(OscillatorPolyChord, ChordNeverStepsAndDoesNotDependOnTheBlockSize) {
    const std::vector<std::vector<int>> chords = {{62, 66, 70}, {48, 52, 55, 60, 64, 67, 72, 76}};
    for (const auto& chord : chords) {
        double reference = 0.0;
        for (const int block : {64, 256, 441, 512}) {
            const auto pass = renderChord(chord, 1, block, 100, Voice{})[0];
            const int from = pass.onAt - 50;
            const int to = pass.offAt + 800;
            double step = 0.0;
            for (int i = from + 1; i < to; ++i)
                step = std::max(step, (double)std::abs(pass.out[(size_t)i] - pass.out[(size_t)i - 1]));
            EXPECT_LT(step, 0.08) << chord.size() << " notes, block " << block;
            // The pitch glides up over 5 ms in steps of one block, so the chord's phases (and its beating) differ a
            // little between block sizes; the loudest peak of the held chord must not.
            double peak = 0.0;
            for (int k = pass.onAt + 480; k < pass.offAt; ++k)
                peak = std::max(peak, (double)std::abs(pass.out[(size_t)k]));
            if (reference == 0.0) {
                reference = peak;
                continue;
            }
            EXPECT_NEAR(peak, reference, 0.1 * reference)
                << chord.size() << " notes, block " << block << " against block 64";
        }
    }
}
