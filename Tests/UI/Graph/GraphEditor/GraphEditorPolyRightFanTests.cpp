// GraphEditorPolyRightFanTests.cpp
//
// A poly Oscillator cabled into a poly VCA carries eight voices per leg. Splitting either module (or turning Poly on
// after a split) must leave the right leg fanned voice by voice, Osc R + v into VCA R + v, exactly like the left fan,
// with nothing summed across: a right leg that carries fewer voices than the left plays a chord quieter on that side.
// Bug kind: leg-asymmetry.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "../../../TestAudioHelpers.h"
#include "AI/AIStateMapper/AIStateMapperInternal.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include <cmath>
#include <string>
#include <vector>

namespace {
using Graph = juce::AudioProcessorGraph;
constexpr int kOscRight = OscillatorModule::kRightBase;
constexpr int kVcaRight = VCAModule::kRightBase;
constexpr int kVoices = 8;

struct Rig {
    AudioEngine engine;
    GraphEditor editor{engine};
    Graph::Node::Ptr osc, vca;

    // `polyFirst`: the chain is built poly and joined (the left fan only); otherwise it is built mono and split.
    explicit Rig(bool polyFirst) {
        editor.setSize(800, 600);
        auto& graph = engine.getGraph();
        osc = graph.addNode(std::make_unique<OscillatorModule>());
        vca = graph.addNode(std::make_unique<VCAModule>());
        for (auto* n : {osc.get(), vca.get()}) {
            setPolyParam(*n->getProcessor(), false);
            setDualIOParam(*n->getProcessor(), !polyFirst);
        }
        if (polyFirst) {
            for (auto* n : {osc.get(), vca.get()})
                setPolyParam(*n->getProcessor(), true);
            for (int v = 0; v < kVoices; ++v)
                graph.addConnection({{osc->nodeID, v}, {vca->nodeID, v}});
        } else {
            graph.addConnection({{osc->nodeID, 0}, {vca->nodeID, 0}});
            graph.addConnection({{osc->nodeID, kOscRight}, {vca->nodeID, kVcaRight}});
        }
        editor.updateComponents();
    }

    bool edge(int from, int to) { return engine.getGraph().isConnected({{osc->nodeID, from}, {vca->nodeID, to}}); }
    void toggleSplit(bool oscillator) {
        auto* proc = (oscillator ? osc : vca)->getProcessor();
        setDualIOParam(*proc, !dynamic_cast<ModuleBase*>(proc)->isDualIO());
    }
    void setPoly(bool oscillator, bool poly) { setPolyParam(*(oscillator ? osc : vca)->getProcessor(), poly); }

    // Both legs fan voice by voice and nothing is summed across.
    void expectFullBothLegs(const std::string& what) {
        for (int v = 0; v < kVoices; ++v) {
            EXPECT_TRUE(edge(v, v)) << what << ": left voice " << v;
            EXPECT_TRUE(edge(kOscRight + v, kVcaRight + v)) << what << ": right voice " << v;
            EXPECT_FALSE(edge(kOscRight + v, v)) << what << ": right summed onto left, voice " << v;
            EXPECT_FALSE(edge(v, kVcaRight + v)) << what << ": left summed onto right, voice " << v;
        }
        int audioEdges = 0;
        for (const auto& c : engine.getGraph().getConnections())
            if (!c.source.isMIDI() && c.source.nodeID == osc->nodeID && c.destination.nodeID == vca->nodeID)
                ++audioEdges;
        EXPECT_EQ(audioEdges, kVoices * 2) << what << ": no stray cable";
    }
};

// Eight channels of constant 1.0: the per-voice gain a poly VCA reads from its CV fan.
class UnityVoiceCv : public juce::AudioProcessor {
public:
    UnityVoiceCv()
        : juce::AudioProcessor(
              BusesProperties().withOutput("out", juce::AudioChannelSet::discreteChannels(kVoices), true)) {}
    const juce::String getName() const override { return "UnityVoiceCv"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill(b.getWritePointer(ch), 1.0f, b.getNumSamples());
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

double rms(const juce::AudioBuffer<float>& buf, int channel, int from) {
    double sum = 0.0;
    const int n = buf.getNumSamples() - from;
    for (int i = from; i < buf.getNumSamples(); ++i)
        sum += (double)buf.getSample(channel, i) * buf.getSample(channel, i);
    return std::sqrt(sum / std::max(1, n));
}

// Plays a four-note chord through the Oscillator and VCA into a stereo output and returns {left, right} RMS.
std::pair<double, double> playedChordLevels(Rig& rig) {
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
    auto cv = graph.addNode(std::make_unique<UnityVoiceCv>());
    for (int v = 0; v < kVoices; ++v)
        graph.addConnection({{cv->nodeID, v}, {rig.vca->nodeID, VCAModule::kPolyCVBase + v}});
    graph.prepareToPlay(sampleRate, block);

    juce::MidiBuffer midi;
    for (int note : {48, 52, 55, 60})
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
    const auto audio = TestAudioHelpers::renderGraphMultiBlock(graph, midi, 16384, block);
    graph.releaseResources();
    graph.removeNode(midiIn->nodeID);
    graph.removeNode(out->nodeID);
    graph.removeNode(cv->nodeID);
    return {rms(audio, 0, 8192), rms(audio, 1, 8192)};
}

void expectChordLevelsMatch(Rig& rig, const std::string& what) {
    const auto [left, right] = playedChordLevels(rig);
    ASSERT_GT(left, 1e-4) << what << ": the chord must make sound";
    ASSERT_GT(right, 1e-4) << what << ": the right leg must make sound";
    EXPECT_NEAR(20.0 * std::log10(left / right), 0.0, 0.5) << what << ": both sides play the same level";
}
} // namespace

// Regression test for AGE281: splitting a joined poly Oscillator / VCA completed only voice 0 of the right leg.
TEST_F(GraphEditorTest, SplittingPolyOscAndVcaInEitherOrderFansTheRightLegVoiceByVoice) {
    for (bool oscFirst : {true, false}) {
        Rig rig(/*polyFirst=*/true);
        const std::string what = oscFirst ? "osc then vca" : "vca then osc";
        rig.toggleSplit(oscFirst);
        rig.toggleSplit(!oscFirst);
        rig.expectFullBothLegs(what);
        expectChordLevelsMatch(rig, what);
    }
}

// Regression test for AGE281: collapsing and re-splitting a poly module must not leave the right fan short.
TEST_F(GraphEditorTest, ReSplittingPolyOscAndVcaRestoresTheFullRightFan) {
    Rig rig(/*polyFirst=*/true);
    for (bool oscillator : {true, false})
        rig.toggleSplit(oscillator);
    for (bool oscillator : {true, false})
        rig.toggleSplit(oscillator); // collapse both
    for (bool oscillator : {false, true})
        rig.toggleSplit(oscillator); // split again
    rig.expectFullBothLegs("collapse then re-split");
    expectChordLevelsMatch(rig, "collapse then re-split");
}

// Regression test for AGE281: split first, then Poly on, left the right leg on one cable.
TEST_F(GraphEditorTest, SplitThenPolyOnFansTheRightLegVoiceByVoice) {
    for (bool oscFirst : {true, false}) {
        Rig rig(/*polyFirst=*/false);
        const std::string what = oscFirst ? "poly osc then vca" : "poly vca then osc";
        rig.setPoly(oscFirst, true);
        rig.setPoly(!oscFirst, true);
        rig.expectFullBothLegs(what);
        expectChordLevelsMatch(rig, what);
    }
}

// Regression test for AGE281: a project saved with the whole left fan but one right cable loads with the full right
// fan.
TEST_F(GraphEditorTest, LoadingCompletesAPolyRightFanThatHoldsOnlyVoiceZero) {
    Rig rig(/*polyFirst=*/true);
    rig.toggleSplit(true);
    rig.toggleSplit(false);
    auto& graph = rig.engine.getGraph();
    for (int v = 1; v < kVoices; ++v)
        graph.removeConnection({{rig.osc->nodeID, kOscRight + v}, {rig.vca->nodeID, kVcaRight + v}});
    ASSERT_FALSE(rig.edge(kOscRight + 1, kVcaRight + 1));
    ASSERT_TRUE(rig.edge(kOscRight, kVcaRight));

    synth::detail::repairSavedJackEdges(graph);

    rig.expectFullBothLegs("after load repair");
}

// Regression test for AGE281: the load repair only completes voices whose left cable is there.
TEST_F(GraphEditorTest, LoadingLeavesAPolyRightVoiceWithoutALeftTwinAlone) {
    Rig rig(/*polyFirst=*/true);
    rig.toggleSplit(true);
    rig.toggleSplit(false);
    auto& graph = rig.engine.getGraph();
    for (int v : {2, 3}) {
        graph.removeConnection({{rig.osc->nodeID, kOscRight + v}, {rig.vca->nodeID, kVcaRight + v}});
        graph.removeConnection({{rig.osc->nodeID, v}, {rig.vca->nodeID, v}});
    }

    synth::detail::repairSavedJackEdges(graph);

    EXPECT_FALSE(rig.edge(kOscRight + 2, kVcaRight + 2));
    EXPECT_FALSE(rig.edge(kOscRight + 3, kVcaRight + 3));
    EXPECT_TRUE(rig.edge(kOscRight + 1, kVcaRight + 1));
}
