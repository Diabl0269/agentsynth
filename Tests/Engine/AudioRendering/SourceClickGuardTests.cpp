// The guard for the "audio-unheard" bug kind at the note edges of every sound source: each source module in the
// registry is played through an ADSR-gated VCA with touching and staccato notes, landing mid-block and on block
// boundaries, in mono and in stereo, and any second-difference spike (ClickDetector.h) at a note start or end
// fails the case. The source's own sound is checked before the envelope as well as after it, because a restart
// that ticks while the envelope is still rising is easy to miss in the product.
//
// Every registered module must be classified here (EveryRegisteredModuleIsClassified), so a new source cannot
// join the product without joining the guard. A source whose onset is itself the instrument's sound is skipped
// by name with its reason, never hidden by a looser limit.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "ClickRenderRig.h"
#include "HostedSineSource.h"
#include "Modules/ADSRModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/VCAModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <set>

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;
const std::vector<int> kPitches = {68, 52, 58, 51, 53, 48, 66, 69, 37, 46, 65, 41, 71, 53, 69, 39};

struct NoteSource {
    const char* name;
    const char* skipReason = nullptr; ///< not played at all: why
    const char* knownClick = nullptr; ///< plays and clicks today: the case is skipped while it does, and FAILS once it
                                      ///< stops so the entry is removed
};

const std::vector<NoteSource>& noteSources() {
    static const std::vector<NoteSource> sources = {
        {"Oscillator"},
        {"Wavetable"},
        {"Pluck"},
        {"Hosted Plugin"},
        {"Noise", "noise has no slope bound, so a step cannot be told from signal; its CV gating is covered by "
                  "NoiseModuleTest.LevelCVThatStartsMidBlockActsWithinThatBlock"},
        {"Sampler", "an empty Sampler plays nothing; sample playback is covered by the Sampler suites"},
        {"Mallet", "a struck bar's onset is a broadband burst, which is the instrument's sound"},
        {"Drum Kit", "a drum hit's onset is a broadband burst, which is the instrument's sound"},
    };
    return sources;
}

// Registered modules that are not played by notes: they process, route or modulate, or bring in audio that is
// not ours to shape. Anything not here and not in noteSources() fails EveryRegisteredModuleIsClassified.
const std::set<std::string>& notNoteSources() {
    static const std::set<std::string> names = {
        "ADSR",
        "Amp Env",
        "Attenuverter",
        "Audio Input",
        "Audio Output",
        "Bitcrusher",
        "Channel Strip",
        "Chorus",
        "Comparator",
        "Compressor",
        "Delay",
        "Distortion",
        "Envelope Follower",
        "External MIDI",
        "Filter",
        "Filter Env",
        "Flanger",
        "Gate",
        "LFO",
        "Limiter",
        "MIDI Keyboard",
        "Macro In",
        "Macro MIDI In",
        "Macro MIDI Out",
        "Macro Out",
        "Macros",
        "Master",
        "Math",
        "Midi Input",
        "Mod Slot",
        "Parametric EQ",
        "Phaser",
        "Pitch Shifter",
        "Poly MIDI",
        "Poly Sequencer",
        "Rec Tap",
        "Reverb",
        "Ring Modulator",
        "Sample & Hold",
        "Sequencer",
        "Track Audio",
        "Track In",
        "VCA",
        "Voice Mixer",
    };
    return names;
}

synth::test::StubBackend& hostedBackend() {
    static synth::test::StubBackend backend(clickrig::sineFactory());
    return backend;
}

// Phases a note may start at: the zero crossing, a quarter wave in, and the two mid-wave values real projects use.
const std::vector<float> kPhases = {0.0f, 90.0f, 250.0f, 274.0f};

// Only the oscillators have a Phase; the other sources are played once.
std::vector<float> phasesFor(const juce::String& name) {
    return name == "Oscillator" || name == "Wavetable" ? kPhases : std::vector<float>{0.0f};
}

std::unique_ptr<juce::AudioProcessor> makeSource(const juce::String& name, bool stereo, float phase) {
    if (name == "Hosted Plugin")
        return clickrig::makeHostedSine(kRate, kBlock, hostedBackend());
    auto source = synth::AIStateMapper::createModule(name);
    if (source == nullptr)
        return source;
    if (name == "Wavetable")
        setParamValue(source.get(), "position", 0.0f); // the first frame of Basic Shapes is a sine
    setParamValue(source.get(), "phase", phase);
    if (stereo && (name == "Oscillator" || name == "Wavetable")) {
        setParamValue(source.get(), "unison", 3.0f);
        setParamValue(source.get(), "detune", 30.0f);
        setParamValue(source.get(), "width", 1.0f);
    }
    return source;
}

juce::String describe(const std::vector<clickdetector::Click>& clicks) {
    juce::String text;
    for (size_t i = 0; i < clicks.size() && i < 4; ++i)
        text << " sample " << clicks[i].index << " (size " << clicks[i].size << ", " << clicks[i].ratio << "x)";
    return text;
}

std::string sanitiseName(const juce::String& s) {
    juce::String out;
    for (auto c : s)
        out += (juce::CharacterFunctions::isLetterOrDigit(c) ? juce::String::charToString(c) : juce::String("_"));
    return out.toStdString();
}

} // namespace

TEST(SourceClickGuard, EveryRegisteredModuleIsClassified) {
    std::set<std::string> classified = notNoteSources();
    for (const auto& s : noteSources())
        classified.insert(s.name);
    for (const auto& name : synth::AIStateMapper::moduleFactoryTypeNames())
        EXPECT_TRUE(classified.count(name.toStdString()) == 1)
            << name
            << " is a registered module that the click guard does not know: if it plays notes add it to "
               "noteSources(), otherwise to notNoteSources()";
    for (const auto& name : classified)
        EXPECT_TRUE(synth::AIStateMapper::moduleFactoryTypeNames().contains(juce::String(name)))
            << name << " is classified but no longer registered";
}

class SourceClickGuard : public ::testing::TestWithParam<NoteSource> {};

TEST_P(SourceClickGuard, NoteStartsAndEndsHaveNoClick) {
    const auto& param = GetParam();
    if (param.skipReason != nullptr)
        GTEST_SKIP() << param.skipReason;

    std::vector<juce::String> problems;
    for (const bool stereo : {false, true}) {
        for (const bool staccato : {false, true}) {
            for (const int step : {6000, 11 * kBlock}) { // mid-block starts, then starts on a block boundary
                for (const float phase : phasesFor(param.name)) {
                    const juce::String label = juce::String(param.name) + (stereo ? " stereo" : " mono") +
                                               (staccato ? " staccato" : " touching") + " step " + juce::String(step) +
                                               " phase " + juce::String((int)phase);
                    auto source = makeSource(param.name, stereo, phase);
                    ASSERT_NE(source, nullptr) << param.name;
                    const auto notes = clickrig::notesEvery(kPitches, step, staccato);
                    clickrig::Envelope env;
                    env.sustain = staccato ? 0.0f : 1.0f;
                    const int total = step * (int)kPitches.size() + 14400;
                    const auto r = clickrig::renderThroughEnvelope(*source, notes, total, env, kRate, kBlock);

                    float peak = 0.0f;
                    for (float v : r.out)
                        peak = std::max(peak, std::abs(v));
                    ASSERT_GT(peak, 0.05f) << label << " produced no signal";

                    // The envelope's own corners, where its attack, decay and release end and where the last note
                    // starts to release, are the only places the post-VCA signal may bend.
                    std::vector<int> corners = {notes.back().off - 4};
                    for (const auto& n : notes) {
                        corners.push_back(n.on + (int)std::lround(env.attack * kRate) - 4);
                        corners.push_back(n.off + (int)std::lround(env.release * kRate) - 4);
                        corners.push_back(n.on + (int)std::lround((env.attack + env.decay) * kRate) - 4);
                    }

                    const auto check = [&](const char* stage, const std::vector<float>& x, bool afterEnvelope) {
                        if (x.empty())
                            return;
                        auto found = clickdetector::findClicks(x);
                        if (afterEnvelope)
                            found = clickdetector::clicksOutside(found, corners, 9);
                        if (!found.empty())
                            problems.push_back(label + " " + stage + ": " + juce::String((int)found.size()) +
                                               " clicks:" + describe(found));
                    };
                    check("source left", r.source, false);
                    check("source right", r.sourceRight, false);
                    check("output left", r.out, true);
                    check("output right", r.outRight, true);
                }
            }
        }
    }

    if (param.knownClick != nullptr) {
        if (problems.empty())
            FAIL() << param.name << " no longer clicks; remove its knownClick entry";
        std::cout << "[guard] known click (" << param.knownClick << "): " << problems.front() << std::endl;
        GTEST_SKIP() << "known click (" << param.knownClick << "): " << problems.front();
    }
    for (const auto& problem : problems)
        ADD_FAILURE() << problem;
}

INSTANTIATE_TEST_SUITE_P(EveryNoteSource, SourceClickGuard, ::testing::ValuesIn(noteSources()),
                         [](const ::testing::TestParamInfo<NoteSource>& info) {
                             return sanitiseName(info.param.name);
                         });

// A poly voice that has never played starts at its note's own sample. Its pitch CV only starts to sound inside the
// block (Poly MIDI glides it up from 0), and the envelope is already rising there, so a voice that waited for the
// next block started at full level: a step of up to the wave's height, bigger the bigger the block. The first
// chord of a fresh Poly MIDI -> Oscillator -> ADSR -> VCA chain, off the block grid, steps no more than the sines'
// own slope (0.052 for this chord) at every block size.
TEST(SourceClickGuard, PolyOscillatorFirstChordStartsOnItsOwnSample) {
    const std::vector<int> chord = {62, 66, 70};
    const int on = 24100;
    const int off = 48100;
    for (const int block : {64, 256, 441, 512}) {
        PolyMidiModule midi;
        OscillatorModule osc;
        ADSRModule adsr;
        VCAModule vca;
        setParamValue(&osc, "poly", 1.0f);
        setParamValue(&osc, "phase", 273.7f);
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

        std::vector<float> out;
        for (int pos = 0; pos < 52000; pos += block) {
            juce::MidiBuffer in;
            for (const int note : chord) {
                if (off >= pos && off < pos + block)
                    in.addEvent(juce::MidiMessage::noteOff(1, note), off - pos);
                if (on >= pos && on < pos + block)
                    in.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)100), on - pos);
            }
            juce::AudioBuffer<float> fan(16, block);
            fan.clear();
            juce::MidiBuffer forMidi(in);
            midi.processBlock(fan, forMidi);
            juce::AudioBuffer<float> o(OscillatorModule::kNumOutputs, block);
            o.clear();
            for (int v = 0; v < 8; ++v) {
                o.copyFrom(v, 0, fan, v, 0, block);
                o.copyFrom(OscillatorModule::kGateBase + v, 0, fan, 8 + v, 0, block);
            }
            juce::MidiBuffer forOsc(in), none;
            osc.processBlock(o, forOsc);
            juce::AudioBuffer<float> env(16, block);
            env.clear();
            for (int v = 0; v < 8; ++v)
                env.copyFrom(v, 0, fan, 8 + v, 0, block);
            adsr.processBlock(env, none);
            juce::AudioBuffer<float> mix(16, block);
            mix.clear();
            for (int v = 0; v < 8; ++v) {
                mix.copyFrom(v, 0, o, v, 0, block);
                mix.copyFrom(8 + v, 0, env, v, 0, block);
            }
            vca.processBlock(mix, none);
            out.insert(out.end(), mix.getReadPointer(0), mix.getReadPointer(0) + block);
        }
        double step = 0.0, peak = 0.0;
        for (int i = on - 100; i < on + 3000; ++i) {
            step = std::max(step, (double)std::abs(out[(size_t)i] - out[(size_t)i - 1]));
            peak = std::max(peak, (double)std::abs(out[(size_t)i]));
        }
        EXPECT_GT(peak, 0.2) << "block " << block << " played nothing";
        EXPECT_LT(step, 0.07) << "block " << block;
    }
}

namespace {

// One mono Oscillator at Glide 0 with Legato on: a held note, an overlapping `interval` above it (no restart), then
// letting go of the top note, which returns to the held one. Each change lands `lead` samples into a block.
std::vector<float> renderLegatoSteps(int waveform, int interval, float phase, int lead, bool overlap) {
    auto osc = std::make_unique<OscillatorModule>();
    setParamValue(osc.get(), "waveform", (float)waveform);
    setParamValue(osc.get(), "legato", 1.0f);
    setParamValue(osc.get(), "phase", phase);
    osc->prepareToPlay(kRate, kBlock);
    const int noteOn = 4 * kBlock + lead, noteOff = 8 * kBlock + lead;
    constexpr int kLow = 57;
    std::vector<float> out;
    for (int pos = 0; pos < 12 * kBlock; pos += kBlock) {
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, kLow, 0.8f), 0);
        if (!overlap && noteOn >= pos && noteOn < pos + kBlock)
            midi.addEvent(juce::MidiMessage::noteOff(1, kLow, 0.0f), noteOn - pos);
        if (noteOn >= pos && noteOn < pos + kBlock)
            midi.addEvent(juce::MidiMessage::noteOn(1, kLow + interval, 0.8f), noteOn - pos);
        if (overlap && noteOff >= pos && noteOff < pos + kBlock)
            midi.addEvent(juce::MidiMessage::noteOff(1, kLow + interval, 0.0f), noteOff - pos);
        juce::AudioBuffer<float> buf(OscillatorModule::kNumOutputs, kBlock);
        buf.clear();
        osc->processBlock(buf, midi);
        out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
    }
    return out;
}

// The largest second difference from sample `from` on, relative to the peak, leaving out a Saw's own wraps: the
// few samples around a fall of a large part of the wave's height, which sound with or without a pitch step.
float worstKinkOfPeak(const std::vector<float>& x, int from) {
    float peak = 1.0e-6f, worst = 0.0f;
    for (float v : x)
        peak = std::max(peak, std::abs(v));
    for (int i = std::max(from, 4); i < (int)x.size() - 4; ++i) {
        bool nearWrap = false;
        for (int k = i - 3; k <= i + 3; ++k)
            nearWrap = nearWrap || x[(size_t)k] - x[(size_t)k + 1] > 0.3f * peak;
        if (!nearWrap)
            worst = std::max(worst, std::abs(x[(size_t)i - 1] - 2.0f * x[(size_t)i] + x[(size_t)i + 1]));
    }
    return worst / peak;
}

} // namespace

// Regression test for FRO1356: a Legato pitch step must not tick. The limit is the note-attack guard's, 0.015 of the
// peak, for a step up and the return down, overlapping (no restart) and touching (a jump), at any phase of the wave.
TEST(SourceClickGuard, LegatoPitchStepsStayUnderTheAttackLimit) {
    constexpr float kLimit = 0.015f;
    for (const int waveform : {0, 2})
        for (const int interval : {2, 5, 12})
            for (const bool overlap : {true, false})
                for (const float phase : {0.0f, 90.0f, 250.0f})
                    for (const int lead : {0, 37, 91, 160, 311}) {
                        const auto x = renderLegatoSteps(waveform, interval, phase, lead, overlap);
                        // Starts past the first note's start, which has no envelope in this rig.
                        const float worst = worstKinkOfPeak(x, 2 * kBlock);
                        EXPECT_LE(worst, kLimit) << "wave " << waveform << " interval " << interval << " overlap "
                                                 << overlap << " phase " << phase << " lead " << lead;
                    }
}
