// GraphEditorDualIOOscVcaTests.cpp
//
// Toggling Split Left/Right on an Oscillator and the VCA it feeds, in every order, must leave the signal as it was:
// the Oscillator's left leg on the VCA's left, its right leg on the VCA's right, and nothing summed across. A joined
// VCA's one jack owns both legs, so splitting the Oscillator beside it used to wire the Oscillator's right leg onto the
// VCA's left as well (a "summed second cable") while it already fed the VCA's right: the left side played twice the
// level of the right, and the extra cable drew as the same line. Bug kind: hidden-but-plays.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "../../../TestAudioHelpers.h"
#include "AI/AIStateMapper/AIStateMapperInternal.h"
#include "Modules/ADSRModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include <cmath>
#include <string>
#include <vector>

namespace {
using Graph = juce::AudioProcessorGraph;
constexpr int kOscRight = OscillatorModule::kRightBase;
constexpr int kVcaRight = VCAModule::kRightBase;

struct Rig {
    AudioEngine engine;
    GraphEditor editor{engine};
    Graph::Node::Ptr osc, vca;

    Rig() {
        editor.setSize(800, 600);
        auto& graph = engine.getGraph();
        osc = graph.addNode(std::make_unique<OscillatorModule>());
        vca = graph.addNode(std::make_unique<VCAModule>());
        setDualIOParam(*osc->getProcessor(), false);
        setDualIOParam(*vca->getProcessor(), false);
        setPolyParam(*osc->getProcessor(), false);
        setPolyParam(*vca->getProcessor(), false);
        // A drag from one joined jack onto another wires L to L and R to R.
        graph.addConnection({{osc->nodeID, 0}, {vca->nodeID, 0}});
        graph.addConnection({{osc->nodeID, kOscRight}, {vca->nodeID, kVcaRight}});
        editor.updateComponents();
    }

    bool edge(int from, int to) { return engine.getGraph().isConnected({{osc->nodeID, from}, {vca->nodeID, to}}); }
    void toggle(bool oscillator) {
        auto* proc = (oscillator ? osc : vca)->getProcessor();
        setDualIOParam(*proc, !dynamic_cast<ModuleBase*>(proc)->isDualIO());
    }
};

double rms(const juce::AudioBuffer<float>& buf, int channel, int from) {
    double sum = 0.0;
    const int n = buf.getNumSamples() - from;
    for (int i = from; i < buf.getNumSamples(); ++i)
        sum += (double)buf.getSample(channel, i) * buf.getSample(channel, i);
    return std::sqrt(sum / std::max(1, n));
}

// Plays note 57 through the Oscillator and the VCA's two legs into a stereo output and returns {left, right} RMS.
std::pair<double, double> playedLevels(Rig& rig) {
    auto& graph = rig.engine.getGraph();
    constexpr double sampleRate = 48000.0;
    constexpr int block = 512;
    graph.setPlayConfigDetails(0, 2, sampleRate, block);
    auto midiIn =
        graph.addNode(std::make_unique<Graph::AudioGraphIOProcessor>(Graph::AudioGraphIOProcessor::midiInputNode));
    auto out =
        graph.addNode(std::make_unique<Graph::AudioGraphIOProcessor>(Graph::AudioGraphIOProcessor::audioOutputNode));
    graph.addConnection({{midiIn->nodeID, Graph::midiChannelIndex}, {rig.osc->nodeID, Graph::midiChannelIndex}});
    graph.addConnection({{rig.vca->nodeID, 0}, {out->nodeID, 0}});
    graph.addConnection({{rig.vca->nodeID, kVcaRight}, {out->nodeID, 1}});
    auto adsr = graph.addNode(std::make_unique<ADSRModule>());
    graph.addConnection({{midiIn->nodeID, Graph::midiChannelIndex}, {adsr->nodeID, Graph::midiChannelIndex}});
    graph.addConnection({{adsr->nodeID, 0}, {rig.vca->nodeID, 1}});
    graph.prepareToPlay(sampleRate, block);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
    const auto audio = TestAudioHelpers::renderGraphMultiBlock(graph, midi, 16384, block);
    graph.releaseResources();
    graph.removeNode(midiIn->nodeID);
    graph.removeNode(out->nodeID);
    graph.removeNode(adsr->nodeID);
    return {rms(audio, 0, 8192), rms(audio, 1, 8192)};
}

std::string sequenceName(const std::vector<bool>& seq) {
    std::string s;
    for (bool oscillator : seq)
        s += oscillator ? "O " : "V ";
    return s.empty() ? "(none)" : s;
}
} // namespace

// Regression test for AGE265: no toggle order leaves the Oscillator's right leg summed onto the VCA's left.
TEST_F(GraphEditorTest, EveryOscVcaSplitToggleOrderKeepsTheLegsApartAndEqualInLevel) {
    double referenceL = 0.0;
    {
        Rig rig;
        const auto levels = playedLevels(rig);
        referenceL = levels.first;
    }
    ASSERT_GT(referenceL, 1e-4) << "the reference chain must make sound";

    for (int length = 0; length <= 5; ++length)
        for (int bits = 0; bits < (1 << length); ++bits) {
            std::vector<bool> seq;
            for (int i = 0; i < length; ++i)
                seq.push_back(((bits >> i) & 1) != 0);

            Rig rig;
            for (bool oscillator : seq) {
                rig.toggle(oscillator);
                ASSERT_FALSE(rig.edge(kOscRight, 0))
                    << "the right leg is never summed onto the VCA's left: " << sequenceName(seq);
            }
            EXPECT_TRUE(rig.edge(0, 0)) << sequenceName(seq);
            EXPECT_TRUE(rig.edge(kOscRight, kVcaRight)) << sequenceName(seq);

            const auto [left, right] = playedLevels(rig);
            EXPECT_NEAR(left / right, 1.0, 0.02) << "both sides play the same level: " << sequenceName(seq);
            EXPECT_NEAR(left / referenceL, 1.0, 0.05) << "toggling does not change the level: " << sequenceName(seq);
        }
}

// Regression test for AGE265: a project saved with the stray cable loses it on load.
TEST_F(GraphEditorTest, LoadingDropsTheRightLegSummedOntoTheLeftBesideItsOwnRightCable) {
    Rig rig;
    rig.toggle(true);
    rig.toggle(false);
    auto& graph = rig.engine.getGraph();
    ASSERT_TRUE(rig.edge(0, 0));
    ASSERT_TRUE(rig.edge(kOscRight, kVcaRight));
    // The saved shape: both split, with the extra cable the old toggle left behind.
    ASSERT_TRUE(graph.addConnection({{rig.osc->nodeID, kOscRight}, {rig.vca->nodeID, 0}}));

    synth::detail::repairSavedJackEdges(graph);

    EXPECT_FALSE(rig.edge(kOscRight, 0)) << "the stray summed cable is gone";
    EXPECT_TRUE(rig.edge(0, 0));
    EXPECT_TRUE(rig.edge(kOscRight, kVcaRight));
}
