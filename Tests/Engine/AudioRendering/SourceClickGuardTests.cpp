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
#include <algorithm>
#include <iostream>
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
        {"Wavetable", nullptr,
         "its note start is a hard phase reset at the start of the block, with no fade: the output steps by up to "
         "the wave's full height when a note lands on a sounding one"},
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

std::unique_ptr<juce::AudioProcessor> makeSource(const juce::String& name, bool stereo) {
    if (name == "Hosted Plugin")
        return clickrig::makeHostedSine(kRate, kBlock, hostedBackend());
    auto source = synth::AIStateMapper::createModule(name);
    if (source == nullptr)
        return source;
    if (name == "Wavetable")
        setParamValue(source.get(), "position", 0.0f); // the first frame of Basic Shapes is a sine
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
                const juce::String label = juce::String(param.name) + (stereo ? " stereo" : " mono") +
                                           (staccato ? " staccato" : " touching") + " step " + juce::String(step);
                auto source = makeSource(param.name, stereo);
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
