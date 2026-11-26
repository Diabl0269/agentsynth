// "Turn Poly on" (the Poly Sequencer hint, a Poly pill) on real chains: one Poly MIDI per voice graph however many
// MIDI sources feed it, every cable it needs, and a chord heard on both sides of the output.

#include "../../../Mixer/ChannelFlow/ChannelFlowTestRigs.h"
#include "../../../Mixer/MasterSafetyOff.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"
#include "MacroSet.h"
#include "Modules/ModuleBase.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/Sequencer/SequencerModuleBase.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "TestPaths.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/PolyChain/PolyChainController.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <tuple>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
constexpr double kRate = 44100.0;
constexpr int kBlock = 256;

void writeWavIfAsked(const juce::AudioBuffer<float>& audio, const char* name) {
    const char* dir = std::getenv("AGENTSYNTH_RENDER_WAV_DIR");
    if (name == nullptr || dir == nullptr || *dir == '\0')
        return;
    auto file = juce::File(juce::String::fromUTF8(dir)).getChildFile(name);
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    auto* stream = new juce::FileOutputStream(file);
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream, kRate, 2, 16, {}, 0));
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
}

double dbBetween(double a, double b) { return 20.0 * std::log10(std::max(a, 1.0e-12) / std::max(b, 1.0e-12)); }

struct ProjectRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    synth::TimelineDoc timeline;
    synth::MidiRemoteProjectDoc midiRemote;
    bool prepared = false;

    ProjectRig() {
        engine.getGraph().setPlayConfigDetails(2, 2, kRate, kBlock);
        undo.setGraphEditor(&editor);
        editor.setSize(3000, 1600);
        editor.getPolyChain().confirmForTest = [](const synth::ui::PolyChainPrompt&,
                                                  std::function<void(synth::ui::PolyChainChoice)> reply) {
            reply(synth::ui::PolyChainChoice::thisTrackOnly);
        };
    }
    ~ProjectRig() {
        if (prepared)
            engine.releaseFromHost();
    }

    bool load(const juce::File& bundle) {
        editor.detachAllModuleComponents();
        const auto result = synth::ProjectBundle::load(bundle, engine.getGraph(), timeline, editor.getPatchDocument(),
                                                       editor.getMacros(), midiRemote);
        EXPECT_TRUE(result.ok) << result.message;
        editor.updateComponents();
        return result.ok;
    }

    NodeID add(const juce::String& type, int x, int y = 100) {
        juce::String unused;
        auto* node = addPlainNodeCFT(engine.getGraph(), type, {x, y}, unused);
        EXPECT_NE(node, nullptr) << type;
        return node != nullptr ? node->nodeID : NodeID{};
    }

    void wire(NodeID from, int fromChannel, NodeID to, int toChannel) {
        EXPECT_TRUE(engine.getGraph().addConnection({{from, fromChannel}, {to, toChannel}}));
    }

    ModuleBase& module(NodeID id) {
        return *dynamic_cast<ModuleBase*>(engine.getGraph().getNodeForId(id)->getProcessor());
    }

    bool connected(NodeID from, int fromChannel, NodeID to, int toChannel) {
        return engine.getGraph().isConnected({{from, fromChannel}, {to, toChannel}});
    }

    std::vector<NodeID> nodesOfType(const juce::String& type) {
        std::vector<NodeID> out;
        for (auto* node : engine.getGraph().getNodes())
            if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()); mb != nullptr && mb->getName() == type)
                out.push_back(node->nodeID);
        return out;
    }

    // Renders `blocks` blocks through the engine; writes them to $AGENTSYNTH_RENDER_WAV_DIR/<wavName> when set.
    std::pair<double, double> renderRms(int blocks, const char* wavName = nullptr) {
        if (!prepared) {
            testutil::turnOffMasterSafetyLimiter(engine.getGraph());
            engine.prepareForHost(kRate, kBlock, 2, 2);
            prepared = true;
        }
        engine.refreshNormalling();
        double l = 0.0, r = 0.0;
        juce::AudioBuffer<float> all(2, blocks * kBlock);
        for (int b = 0; b < blocks; ++b) {
            juce::AudioBuffer<float> buffer(2, kBlock);
            buffer.clear();
            juce::MidiBuffer midi;
            engine.processHostBlock(buffer, midi);
            for (int i = 0; i < kBlock; ++i) {
                l += (double)buffer.getSample(0, i) * buffer.getSample(0, i);
                r += (double)buffer.getSample(1, i) * buffer.getSample(1, i);
            }
            for (int ch = 0; ch < 2; ++ch)
                all.copyFrom(ch, b * kBlock, buffer, ch, 0, kBlock);
        }
        writeWavIfAsked(all, wavName);
        const double n = (double)blocks * kBlock;
        return {std::sqrt(l / n), std::sqrt(r / n)};
    }
};

void playChord(juce::AudioProcessorGraph& graph, NodeID sequencer) {
    auto* module = dynamic_cast<synth::SequencerModuleBase*>(graph.getNodeForId(sequencer)->getProcessor());
    ASSERT_NE(module, nullptr);
    auto pattern = module->getPattern();
    for (auto& step : pattern.steps) {
        step.chord = synth::SequencerChord::Major;
        step.pitch = 60;
        step.gate = 1.0f;
    }
    module->setPattern(pattern);
    for (auto* p : module->getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p); b != nullptr && b->paramID == "run")
            *b = true;
}

juce::File fixture(const char* name) { return juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile(name); }

} // namespace

namespace {

struct ChainShape {
    bool envelope = false;     // ADSR + VCA after the Oscillator
    bool filter = false;       // a Filter, then a Gate whose one joined Audio jack takes the Filter's legs
    bool filterDualIO = true;  // the Filter's Left and Right both cabled into the Gate
    bool secondSource = false; // a Track In feeding the same voices as the sequencer
    const char* wav = nullptr;
};

struct TurnedOn {
    int polyMidiCount = 0;
    double left = 0.0;
    double right = 0.0;
};

// A mono chain cabled the way a user drags it, then "Turn Poly on" clicked once on the Oscillator.
TurnedOn turnPolyOnAndPlay(ProjectRig& rig, const ChainShape& shape) {
    auto& graph = rig.engine.getGraph();
    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    const auto seq = rig.add("Poly Sequencer", 0);
    const auto osc = rig.add("Oscillator", 400);
    const auto out = addAudioOutputNode(graph, 2400, 100)->nodeID;
    setDualIOParam(rig.module(osc), false);
    rig.wire(seq, midi, osc, midi);
    NodeID adsr;
    NodeID tracks;
    if (shape.secondSource) {
        tracks = rig.add("Track In", 0, 500);
        rig.wire(tracks, midi, osc, midi);
    }
    rig.editor.updateComponents();
    auto tail = osc;
    if (shape.envelope) {
        adsr = rig.add("ADSR", 400, 500);
        const auto vca = rig.add("VCA", 800);
        setDualIOParam(rig.module(vca), false);
        rig.wire(seq, midi, adsr, midi);
        if (shape.secondSource)
            rig.wire(tracks, midi, adsr, midi);
        rig.editor.updateComponents();
        rig.editor.connectPorts(osc, 0, vca, 0, false, false);
        rig.editor.connectPorts(adsr, 0, vca, 1, false, false);
        tail = vca;
    }
    if (shape.filter) {
        const auto filter = rig.add("Filter", 1200);
        const auto gate = rig.add("Gate", 1600);
        setDualIOParam(rig.module(filter), shape.filterDualIO);
        setDualIOParam(rig.module(gate), false);
        rig.editor.updateComponents();
        rig.editor.connectPorts(tail, 0, filter, 0, false, false);
        rig.editor.connectPorts(filter, 0, gate, 0, false, false);
        if (shape.filterDualIO) {
            rig.editor.connectPorts(tail, 0, filter, 1, false, false);
            rig.editor.connectPorts(filter, 1, gate, 0, false, false);
        }
        tail = gate;
    }
    rig.editor.connectPorts(tail, 0, out, 0, false, false);

    rig.editor.getPolyChain().turnPolyOn(osc);

    TurnedOn result;
    const auto polyMidis = rig.nodesOfType("Poly MIDI");
    result.polyMidiCount = (int)polyMidis.size();
    if (polyMidis.size() != 1)
        return result;
    const auto polyMidi = polyMidis[0];
    EXPECT_TRUE(rig.connected(seq, midi, polyMidi, midi)) << "the sequencer plays through Poly MIDI";
    for (int voice = 0; voice < 8; ++voice)
        EXPECT_TRUE(rig.connected(polyMidi, voice, osc, voice)) << "Poly MIDI pitch reaches Oscillator voice " << voice;
    if (shape.secondSource)
        EXPECT_TRUE(rig.connected(tracks, midi, polyMidi, midi)) << "the second source merges into the same Poly MIDI";
    if (shape.envelope)
        for (int voice = 0; voice < 8; ++voice)
            EXPECT_TRUE(rig.connected(polyMidi, 8 + voice, adsr, voice))
                << "Poly MIDI gate reaches ADSR voice " << voice;
    EXPECT_GE(rig.editor.buildVisibleCables().size(), 3u);

    playChord(graph, seq);
    std::tie(result.left, result.right) = rig.renderRms(344, shape.wav); // two seconds
    return result;
}

void expectBothSidesEven(const TurnedOn& on) {
    EXPECT_EQ(on.polyMidiCount, 1);
    EXPECT_GT(on.left, 0.01) << "the chord is heard on the Left";
    EXPECT_GT(on.right, 0.01) << "the chord is heard on the Right";
    EXPECT_LT(std::abs(dbBetween(on.left, on.right)), 1.0) << "L " << on.left << " R " << on.right;
}

} // namespace

TEST(PolyTurnOn, OneOscillatorFromASequencerGetsOnePolyMidiAndSoundsOnBothSides) {
    ProjectRig rig;
    expectBothSidesEven(turnPolyOnAndPlay(rig, {}));
}

TEST(PolyTurnOn, OscillatorAndEnvelopeShareOnePolyMidiAndSoundOnBothSides) {
    ProjectRig rig;
    expectBothSidesEven(turnPolyOnAndPlay(rig, {.envelope = true}));
}

TEST(PolyTurnOn, ADualFilterWithBothLegsIntoAGateSoundsOnBothSides) {
    ProjectRig rig;
    expectBothSidesEven(turnPolyOnAndPlay(rig, {.envelope = true, .filter = true, .wav = "chain-dual-filter.wav"}));
}

// Regression test for FRO2366: a poly voice fan into a Gate's one Audio jack wired voice 0 to Left and voice 1 to
// Right.
TEST(PolyTurnOn, AJoinedFilterIntoAGateSoundsOnBothSides) {
    ProjectRig rig;
    expectBothSidesEven(turnPolyOnAndPlay(rig, {.envelope = true, .filter = true, .filterDualIO = false}));
}

TEST(PolyTurnOn, APolyFanIntoAJoinedStereoPairSumsEveryVoiceOntoLeft) {
    ProjectRig rig;
    const auto filter = rig.add("Filter", 0);
    const auto gate = rig.add("Gate", 400);
    setDualIOParam(rig.module(filter), false);
    setDualIOParam(rig.module(gate), false);
    setPolyParam(rig.module(filter), true);
    const auto link = GraphEditor::resolvePolyLink(&rig.module(filter), 0, &rig.module(gate), 0);
    EXPECT_EQ(link.voiceCount, 8);
    EXPECT_EQ(link.sourceRawChannel, 0);
    EXPECT_EQ(link.destRawChannel, 0);
    EXPECT_EQ(link.destStride, 0) << "every voice lands on Left; Right borrows it by normalling";

    rig.editor.updateComponents();
    rig.editor.connectPorts(filter, 0, gate, 0, false, false);
    int cables = 0;
    for (const auto& cable : rig.editor.buildVisibleCables())
        cables += cable.id.dstUid == gate.uid ? 1 : 0;
    EXPECT_EQ(cables, 1) << "eight voices are one cable on the canvas";
    rig.engine.refreshNormalling();
    EXPECT_TRUE(rig.module(gate).isNormalLeftToRight()) << "the joined Gate's Right leg plays what Left gets";
}

// Regression test for FRO2366: a Track In and a Poly Sequencer both feeding the voices each got a Poly MIDI.
TEST(PolyTurnOn, TwoMidiSourcesFeedingTheSameVoicesGetOnePolyMidi) {
    ProjectRig rig;
    expectBothSidesEven(turnPolyOnAndPlay(rig, {.envelope = true, .filter = true, .secondSource = true}));
}

// Regression test for FRO2366: the saved "test poly seq" project, switched Poly off and on, had two Poly MIDI nodes.
TEST(PolyTurnOn, TheSavedTestPolySeqProjectGetsOnePolyMidiAndBothSidesAfterPolyOffAndOn) {
    ProjectRig rig;
    ASSERT_TRUE(rig.load(fixture("test-poly-seq.agsproj")));
    const auto seq = rig.nodesOfType("Poly Sequencer");
    const auto osc = rig.nodesOfType("Oscillator");
    ASSERT_EQ(seq.size(), 1u);
    ASSERT_EQ(osc.size(), 1u);
    playChord(rig.engine.getGraph(), seq[0]);
    // As saved (by a build that paired a poly fan's voice 0 with Left and voice 1 with Right): Right is silent.
    const auto [savedLeft, savedRight] = rig.renderRms(344, "test-poly-seq-as-saved.wav");
    EXPECT_GT(savedLeft, 0.01);
    EXPECT_LT(savedRight, 1.0e-4);
    rig.editor.getPolyChain().polyPillClicked(osc[0], false);
    rig.editor.getPolyChain().turnPolyOn(osc[0]);
    TurnedOn on;
    on.polyMidiCount = (int)rig.nodesOfType("Poly MIDI").size();
    std::tie(on.left, on.right) = rig.renderRms(344, "test-poly-seq-after-poly-on.wav");
    expectBothSidesEven(on);
}
