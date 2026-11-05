// SequencerChordRenderTests.cpp -- what a Poly Sequencer chord step sounds like, rendered and measured: wired to an
// Oscillator whose Poly switch is on (through the Poly MIDI node the switch inserts) every note of the chord is heard;
// with the switch off the Oscillator plays one note of it. This is the situation the card's hint names.
#include "../../Mixer/ChannelFlow/ChannelFlowTestRigs.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/ChannelFlows/PolyVoiceGraph.h"
#include "Modules/PolySequencerModule.h"
#include "Modules/Sequencer/SequencerModuleBase.h"
#include "SequencerTestSupport.h"
#include <cmath>
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
