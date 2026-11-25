// SequencerChordRenderTests.cpp -- what a Poly Sequencer chord step sounds like, rendered and measured: wired to an
// Oscillator whose Poly switch is on (through the Poly MIDI node the switch inserts) every note of the chord is heard;
// with the switch off the Oscillator plays one note of it. This is the situation the card's hint names.
#include "../../Mixer/ChannelFlow/ChannelFlowTestRigs.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/ChannelFlows/PolyVoiceGraph.h"
#include "Modules/PolySequencerModule.h"
#include "Modules/Sequencer/SequencerModuleBase.h"
#include "SequencerTestSupport.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <juce_audio_formats/juce_audio_formats.h>
#include <numbers>
#include <vector>

namespace {

constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;
constexpr int kCMajorRoot = 60;
const std::vector<int> kChord = {60, 64, 67};

float frequencyOf(int pitch) { return 440.0f * std::pow(2.0f, (float)(pitch - 69) / 12.0f); }

/** The magnitude of `signal` at `hz` (a Hann-windowed single-bin DFT). */
double magnitudeAt(const std::vector<float>& signal, double sampleRate, double hz) {
    double re = 0.0;
    double im = 0.0;
    const double n = (double)signal.size();
    for (size_t i = 0; i < signal.size(); ++i) {
        const double window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * (double)i / (n - 1.0));
        const double phase = 2.0 * std::numbers::pi * hz * (double)i / sampleRate;
        re += window * signal[i] * std::cos(phase);
        im += window * signal[i] * std::sin(phase);
    }
    return std::sqrt(re * re + im * im);
}

/** A chord step on a Poly Sequencer wired to a Sine Oscillator and the output; renders the first step. */
std::vector<float> renderChord(bool polyOn) {
    HostedPatchCFT patch;
    auto& graph = patch.engine.getGraph();
    juce::String unused;
    auto* sequencer = addPlainNodeCFT(graph, "Poly Sequencer", {0, 0}, unused);
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {300, 0}, unused);
    EXPECT_NE(sequencer, nullptr);
    EXPECT_NE(osc, nullptr);
    if (sequencer == nullptr || osc == nullptr)
        return {};
    auto* module = dynamic_cast<synth::SequencerModuleBase*>(sequencer->getProcessor());
    auto pattern = module->getPattern();
    pattern.steps[0].chord = synth::SequencerChord::Major;
    pattern.steps[0].pitch = (std::uint8_t)kCMajorRoot;
    module->setPattern(pattern);
    seqtest::setChoice(*osc->getProcessor(), "waveform", "Sine");
    seqtest::run(*sequencer->getProcessor(), true);

    graph.addConnection({{sequencer->nodeID, kMidi}, {osc->nodeID, kMidi}});
    if (polyOn) {
        // What the Poly switch does: the Oscillator goes poly and a Poly MIDI node joins the chain. A poly
        // Oscillator puts each voice on its own channels, which a Voice Mixer sums for the output.
        synth::applyPolyVoiceGraph(graph, {osc->nodeID}, true, {});
        auto* mixer = synth::addVoiceMixerForPolyInstrument(graph, *osc, {600, 0}, unused);
        EXPECT_NE(mixer, nullptr);
        if (mixer == nullptr)
            return {};
        graph.addConnection({{mixer->nodeID, 0}, {patch.output->nodeID, 0}});
        graph.addConnection({{mixer->nodeID, 1}, {patch.output->nodeID, 1}});
    } else {
        const int right = dynamic_cast<ModuleBase*>(osc->getProcessor())->rightAudioLegChannel();
        graph.addConnection({{osc->nodeID, 0}, {patch.output->nodeID, 0}});
        graph.addConnection({{osc->nodeID, right}, {patch.output->nodeID, 1}});
    }
    return patch.render(60); // 60 blocks of 256 samples: the first step's notes, about a third of a second
}

int heardPitches(const std::vector<float>& left) {
    std::vector<float> mono;
    for (size_t i = 0; i + 1 < left.size(); i += 2) // render() interleaves the two channels
        mono.push_back(left[i]);
    std::vector<double> strengths;
    for (const int pitch : kChord)
        strengths.push_back(magnitudeAt(mono, 44100.0, frequencyOf(pitch)));
    const double loudest = *std::max_element(strengths.begin(), strengths.end());
    if (loudest <= 1.0e-3)
        return 0;
    int heard = 0;
    for (const double s : strengths)
        if (s > 0.2 * loudest)
            ++heard;
    return heard;
}

} // namespace

TEST(SequencerChordRender, AMajorChordThroughAPolyOscillatorIsThreeNotesAndThroughAMonoOneIsOne) {
    const int poly = heardPitches(renderChord(true));
    const int mono = heardPitches(renderChord(false));
    EXPECT_GE(poly, 3) << "every note of the chord sounds when the Oscillator is poly";
    EXPECT_EQ(mono, 1) << "a mono Oscillator plays one note of the chord";
}

namespace {

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;
};

// HostedPatchCFT::render lays each block out as its Left samples then its Right samples.
Stereo splitBlocks(const std::vector<float>& samples) {
    Stereo out;
    const size_t block = (size_t)kBlockSizeMCH;
    for (size_t at = 0; at + 2 * block <= samples.size(); at += 2 * block) {
        out.left.insert(out.left.end(), samples.begin() + (long)at, samples.begin() + (long)(at + block));
        out.right.insert(out.right.end(), samples.begin() + (long)(at + block),
                         samples.begin() + (long)(at + 2 * block));
    }
    return out;
}

double rmsOf(const std::vector<float>& x) {
    double sum = 0.0;
    for (const float v : x)
        sum += (double)v * v;
    return std::sqrt(sum / (double)std::max<size_t>(1, x.size()));
}

double dbOf(double ratio) { return 20.0 * std::log10(std::max(ratio, 1.0e-12)); }

void writeWavIfAsked(const Stereo& audio, const char* name) {
    const char* dir = std::getenv("AGENTSYNTH_RENDER_WAV_DIR");
    if (dir == nullptr || *dir == '\0')
        return;
    juce::File file = juce::File(juce::String::fromUTF8(dir)).getChildFile(name);
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    auto* stream = new juce::FileOutputStream(file);
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream, kSampleRateMCH, 2, 16, {}, 0));
    ASSERT_NE(writer, nullptr);
    juce::AudioBuffer<float> buffer(2, (int)audio.left.size());
    buffer.copyFrom(0, 0, audio.left.data(), (int)audio.left.size());
    buffer.copyFrom(1, 0, audio.right.data(), (int)audio.right.size());
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

// A Poly Sequencer chord on every step -> Poly MIDI -> poly Oscillator, the Oscillator's collapsed jack cabled to
// Audio Output Left the way a cable drop wires it (GraphEditor::connectPorts), no Voice Mixer.
Stereo renderChordIntoOutput(synth::SequencerChord chord) {
    HostedPatchCFT patch;
    auto& graph = patch.engine.getGraph();
    GraphEditor editor(patch.engine);
    juce::String unused;
    auto* sequencer = addPlainNodeCFT(graph, "Poly Sequencer", {0, 0}, unused);
    auto* osc = addPlainNodeCFT(graph, "Oscillator", {300, 0}, unused);
    EXPECT_TRUE(sequencer != nullptr && osc != nullptr);
    if (sequencer == nullptr || osc == nullptr)
        return {};
    auto* module = dynamic_cast<synth::SequencerModuleBase*>(sequencer->getProcessor());
    auto pattern = module->getPattern();
    for (auto& step : pattern.steps) {
        step.chord = chord;
        step.pitch = (std::uint8_t)kCMajorRoot;
        step.gate = 1.0f;
    }
    module->setPattern(pattern);
    seqtest::setChoice(*osc->getProcessor(), "waveform", "Sine");
    seqtest::run(*sequencer->getProcessor(), true);
    graph.addConnection({{sequencer->nodeID, kMidi}, {osc->nodeID, kMidi}});
    synth::applyPolyVoiceGraph(graph, {osc->nodeID}, true, {});

    editor.updateComponents();
    editor.connectPorts(osc->nodeID, 0, patch.output->nodeID, 0, false, false);
    patch.engine.refreshNormalling();

    Stereo audio = splitBlocks(patch.render(344)); // two seconds
    return audio;
}

// How strongly each chord pitch is present in `signal`, as a fraction of the loudest of them.
// Measured over the first half second: every step restarts the notes' phase, so a longer window smears the lines.
std::vector<double> chordStrengths(const std::vector<float>& signal, const std::vector<int>& pitches) {
    const std::vector<float> head(signal.begin(), signal.begin() + (long)std::min<size_t>(signal.size(), 22050));
    std::vector<double> mags;
    for (const int pitch : pitches)
        mags.push_back(magnitudeAt(head, kSampleRateMCH, frequencyOf(pitch)));
    const double loudest = std::max(1.0e-9, *std::max_element(mags.begin(), mags.end()));
    for (auto& m : mags)
        m /= loudest;
    return mags;
}

} // namespace

// Regression test for FRO2280: a poly Oscillator cabled straight to Audio Output sums every voice onto the leg it
// lands on, so a Poly Sequencer chord plays all its notes (it used to play only the root), and Right borrows Left.
TEST(SequencerChordRender, PolyFanIntoAudioOutputWiresEveryVoice) {
    const auto single = renderChordIntoOutput(synth::SequencerChord::Unison);
    const auto major = renderChordIntoOutput(synth::SequencerChord::Major);
    const auto minor = renderChordIntoOutput(synth::SequencerChord::Minor);
    ASSERT_FALSE(major.left.empty());
    writeWavIfAsked(major, "chord-major.wav");
    writeWavIfAsked(minor, "chord-minor.wav");

    std::cout << "[rms] one note: L " << rmsOf(single.left) << " R " << rmsOf(single.right) << "; major chord: L "
              << rmsOf(major.left) << " R " << rmsOf(major.right) << std::endl;

    // Every chord note is in Left, well above the off-chord pitches between them.
    const auto majorHeard = chordStrengths(major.left, {60, 64, 67});
    for (size_t i = 0; i < majorHeard.size(); ++i)
        EXPECT_GT(majorHeard[i], 0.4) << "major chord note " << i << " is missing from Left";
    const auto offChord = chordStrengths(major.left, {60, 62, 63, 65, 66, 68});
    const double between = std::max({offChord[1], offChord[2], offChord[3], offChord[4], offChord[5]});
    EXPECT_LT(between, 0.2) << "off-chord pitches should stay well below the chord notes";

    // Right borrows Left by normalling: within 1 dB.
    EXPECT_NEAR(dbOf(rmsOf(major.right) / rmsOf(major.left)), 0.0, 1.0);
    EXPECT_NEAR(dbOf(rmsOf(single.right) / rmsOf(single.left)), 0.0, 1.0);

    // Major -> Minor moves the third from E to E flat.
    const auto minorHeard = chordStrengths(minor.left, {60, 63, 67, 64});
    EXPECT_GT(minorHeard[1], 0.4) << "the minor third is present";
    EXPECT_LT(minorHeard[3], 0.2) << "the major third is gone";
    EXPECT_GT(minorHeard[0], 0.4);
    EXPECT_GT(minorHeard[2], 0.4);
}
