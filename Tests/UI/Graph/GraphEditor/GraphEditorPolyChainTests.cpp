// A user click on a card's Poly pill switches the whole voice graph (Source/UI/Graph/PolyChain): the connected
// modules flip together, a Poly MIDI node is inserted or removed, the question asked when other tracks are involved,
// the single undo step, the toast, and a new module joining the poly state of the first module it is cabled to.
// Programmatic sets never propagate. The pure planning is in Tests/Mixer/ChannelFlow/PolyVoiceGraphTests.cpp.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/MasterModule.h"
#include "Modules/PolyMidiModule.h"
#include "UI/Graph/PolyChain/PolyChainController.h"
#include <bit>
#include <set>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using Connection = juce::AudioProcessorGraph::Connection;
using synth::ui::PolyChainChoice;
constexpr int kMidi = juce::AudioProcessorGraph::midiChannelIndex;

struct ChainRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    std::vector<synth::PolyTrackRef> tracks;
    std::vector<synth::ui::PolyChainPrompt> prompts;
    PolyChainChoice answer = PolyChainChoice::thisTrackOnly;
    std::vector<juce::String> toasts;
    std::function<void()> toastAction;
    int x = 0;

    ChainRig() {
        engine.getGraph().setPlayConfigDetails(2, 2, 44100.0, 512);
        undo.setGraphEditor(&editor);
        editor.setSize(3000, 1600);
        auto& chain = editor.getPolyChain();
        chain.trackProvider = [this] { return tracks; };
        chain.confirmForTest = [this](const synth::ui::PolyChainPrompt& prompt,
                                      std::function<void(PolyChainChoice)> reply) {
            prompts.push_back(prompt);
            reply(answer);
        };
        chain.showToast = [this](const juce::String& message, const juce::String&, std::function<void()> action,
                                 const juce::String&) {
            toasts.push_back(message);
            toastAction = std::move(action);
        };
    }

    juce::AudioProcessorGraph& graph() { return engine.getGraph(); }

    NodeID add(const juce::String& type, juce::String* uuidOut = nullptr) {
        auto processor = synth::AIStateMapper::createModule(type);
        auto node = graph().addNode(std::move(processor));
        const auto uuid = juce::Uuid().toDashedString();
        node->properties.set("uuid", uuid);
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            module->setNodeUuid(uuid);
        node->properties.set("x", x += 320);
        node->properties.set("y", 100);
        if (uuidOut != nullptr)
            *uuidOut = uuid;
        editor.updateComponents();
        return node->nodeID;
    }
    juce::AudioProcessor* proc(NodeID id) { return graph().getNodeForId(id)->getProcessor(); }
    void midi(NodeID from, NodeID to) { graph().addConnection({{from, kMidi}, {to, kMidi}}); }
    void wire(NodeID from, int fromCh, NodeID to, int toCh) { graph().addConnection({{from, fromCh}, {to, toCh}}); }
    bool isPoly(NodeID id) { return synth::isProcessorPoly(proc(id)); }
    void setPoly(NodeID id, bool poly) { synth::setProcessorPoly(proc(id), poly); }

    int count(NodeID from, NodeID to) {
        int n = 0;
        for (const auto& c : graph().getConnections())
            if (c.source.nodeID == from && c.destination.nodeID == to)
                ++n;
        return n;
    }
    std::vector<NodeID> polyMidis() {
        std::vector<NodeID> found;
        for (auto* node : graph().getNodes())
            if (dynamic_cast<PolyMidiModule*>(node->getProcessor()) != nullptr)
                found.push_back(node->nodeID);
        return found;
    }
    std::set<std::tuple<juce::uint32, int, juce::uint32, int>> cables() {
        std::set<std::tuple<juce::uint32, int, juce::uint32, int>> out;
        for (const auto& c : graph().getConnections())
            out.insert(
                {c.source.nodeID.uid, c.source.channelIndex, c.destination.nodeID.uid, c.destination.channelIndex});
        return out;
    }

    juce::ToggleButton* pill(NodeID id) {
        auto* comp = findModuleComp(editor, proc(id));
        if (comp == nullptr)
            return nullptr;
        for (auto* child : comp->getChildren())
            if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child))
                if (toggle->getButtonText() == "Poly")
                    return toggle;
        return nullptr;
    }
    void clickPill(NodeID id) {
        auto* button = pill(id);
        ASSERT_NE(button, nullptr);
        button->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
};

// Midi Input -> Osc (+ ADSR), Osc -> Filter -> VCA -> Audio Output, ADSR -> VCA gain.
struct Instrument {
    NodeID source, osc, filter, adsr, vca, out;
};
Instrument buildInstrument(ChainRig& rig) {
    Instrument i;
    i.source = rig.add("Midi Input");
    i.osc = rig.add("Oscillator");
    i.filter = rig.add("Filter");
    i.adsr = rig.add("ADSR");
    i.vca = rig.add("VCA");
    i.out = rig.add("Audio Output");
    rig.midi(i.source, i.osc);
    rig.midi(i.source, i.adsr);
    rig.wire(i.osc, 0, i.filter, 0);
    rig.wire(i.filter, 0, i.vca, 0);
    rig.wire(i.adsr, 0, i.vca, 1);
    rig.wire(i.vca, 0, i.out, 0);
    return i;
}

// Plays `notes` held for a few blocks through the Hosted engine; returns the peak of the output.
float playChord(ChainRig& rig, const std::vector<int>& notes) {
    rig.engine.prepareForHost(44100.0, 512, 2, 2);
    float peak = 0.0f;
    for (int block = 0; block < 12; ++block) {
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        if (block == 0)
            for (const int note : notes)
                midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
        rig.engine.processHostBlock(buffer, midi);
        peak = std::max(peak, buffer.getMagnitude(0, 512));
    }
    rig.engine.releaseFromHost();
    return peak;
}

int heldVoices(ChainRig& rig) {
    int voices = 0;
    for (const auto id : rig.polyMidis())
        voices +=
            std::popcount(static_cast<unsigned>(dynamic_cast<PolyMidiModule*>(rig.proc(id))->getActiveVoiceMask()));
    return voices;
}

} // namespace

TEST_F(GraphEditorTest, PolyPillCarriesTheVoiceGraphTooltipAndKeepsItsName) {
    ChainRig rig;
    const auto osc = rig.add("Oscillator");
    auto* pill = rig.pill(osc);
    ASSERT_NE(pill, nullptr);
    EXPECT_EQ(pill->getTooltip(), "Play several notes at once. Switches every connected module in this track.");
    EXPECT_EQ(pill->getButtonText(), "Poly");
    EXPECT_FALSE(pill->getClickingTogglesState());
}

TEST_F(GraphEditorTest, ClickingPolyOnTheOscillatorSwitchesTheChainAndAChordSoundsOnSeveralVoices) {
    ChainRig rig;
    const auto i = buildInstrument(rig);

    rig.clickPill(i.osc);

    for (const auto id : {i.osc, i.filter, i.adsr, i.vca})
        EXPECT_TRUE(rig.isPoly(id));
    EXPECT_TRUE(rig.pill(i.filter)->getToggleState()) << "every card's pill follows its parameter";
    const auto polyMidis = rig.polyMidis();
    ASSERT_EQ(polyMidis.size(), 1u);
    const auto pm = polyMidis.front();
    EXPECT_EQ(rig.count(i.source, pm), 1);
    for (int voice = 0; voice < 8; ++voice) {
        EXPECT_TRUE(rig.graph().isConnected({{pm, voice}, {i.osc, voice}})) << voice;
        EXPECT_TRUE(rig.graph().isConnected({{pm, 8 + voice}, {i.adsr, voice}})) << voice;
    }
    EXPECT_GE(rig.count(i.osc, i.filter), 8) << "the audio cables fan out to the voices";
    EXPECT_GE(rig.count(i.filter, i.vca), 8);
    ASSERT_EQ(rig.toasts.size(), 1u);
    EXPECT_EQ(rig.toasts[0], "Made 4 modules poly");

    EXPECT_GT(playChord(rig, {60, 64, 67}), 0.0f);
    EXPECT_EQ(heldVoices(rig), 3);
}

TEST_F(GraphEditorTest, PolyClickOnAnyModuleOfTheChainSwitchesTheSameSet) {
    for (int which = 0; which < 4; ++which) {
        ChainRig rig;
        const auto i = buildInstrument(rig);
        const NodeID starts[] = {i.filter, i.adsr, i.vca, i.osc};
        rig.clickPill(starts[which]);
        for (const auto id : {i.osc, i.filter, i.adsr, i.vca})
            EXPECT_TRUE(rig.isPoly(id)) << which;
        EXPECT_EQ(rig.polyMidis().size(), 1u) << which;
    }
}

TEST_F(GraphEditorTest, ParallelOscillatorsAndEnvelopesAllSwitchAndShareOnePolyMidi) {
    ChainRig rig;
    const auto i = buildInstrument(rig);
    const auto osc2 = rig.add("Oscillator");
    const auto adsr2 = rig.add("ADSR");
    rig.midi(i.source, osc2);
    rig.midi(i.source, adsr2);
    rig.wire(osc2, 0, i.filter, 0);
    rig.wire(adsr2, 0, i.vca, 1);

    rig.clickPill(i.vca);

    for (const auto id : {i.osc, osc2, i.filter, i.adsr, adsr2, i.vca})
        EXPECT_TRUE(rig.isPoly(id));
    ASSERT_EQ(rig.polyMidis().size(), 1u);
    const auto pm = rig.polyMidis().front();
    EXPECT_EQ(rig.count(pm, osc2), 8);
    EXPECT_EQ(rig.count(pm, adsr2), 8);
    EXPECT_EQ(rig.toasts.back(), "Made 6 modules poly");
}

TEST_F(GraphEditorTest, AModuleInsideAMacroAndOneWiredIntoItBothSwitch) {
    ChainRig rig;
    const auto i = buildInstrument(rig);
    rig.editor.setSelectedNodes({i.filter, i.vca});
    const auto macroId = rig.editor.getMacroController().groupSelectionIntoMacro(false);
    ASSERT_FALSE(macroId.isEmpty());

    rig.editor.getPolyChain().polyPillClicked(i.osc, true); // outside the macro, wired into it

    for (const auto id : {i.osc, i.filter, i.adsr, i.vca})
        EXPECT_TRUE(rig.isPoly(id));

    rig.editor.getPolyChain().polyPillClicked(i.filter, false); // inside the macro, wired to the outside
    for (const auto id : {i.osc, i.filter, i.adsr, i.vca})
        EXPECT_FALSE(rig.isPoly(id));
}

TEST_F(GraphEditorTest, ATrackBoundaryStopsTheSwitchWithoutAskingAndTheOtherTrackStaysMono) {
    ChainRig rig;
    juce::String uuidA, uuidB;
    const auto trackA = rig.add("Track In", &uuidA);
    const auto a = buildInstrument(rig);
    const auto trackB = rig.add("Track In", &uuidB);
    const auto b = buildInstrument(rig);
    rig.tracks = {{uuidA, "Bass"}, {uuidB, "Lead"}};
    const auto stripA = rig.add("Channel Strip");
    const auto stripB = rig.add("Channel Strip");
    const auto master = rig.add("Master");
    rig.midi(trackA, a.osc);
    rig.midi(trackB, b.osc);
    rig.wire(a.vca, 0, stripA, 0);
    rig.wire(b.vca, 0, stripB, 0);
    rig.wire(stripA, 0, master, MasterModule::kMixLeft);
    rig.wire(stripB, 0, master, MasterModule::kMixLeft);

    rig.clickPill(a.osc);

    EXPECT_TRUE(rig.prompts.empty());
    for (const auto id : {a.osc, a.filter, a.adsr, a.vca})
        EXPECT_TRUE(rig.isPoly(id));
    for (const auto id : {b.osc, b.filter, b.adsr, b.vca})
        EXPECT_FALSE(rig.isPoly(id));
}

namespace {
// Track A's oscillator and track B's oscillator both play through track A's filter and VCA.
struct TwoTrackRig {
    ChainRig rig;
    Instrument a;
    NodeID oscB{};
    TwoTrackRig() {
        juce::String uuidA, uuidB;
        const auto trackA = rig.add("Track In", &uuidA);
        a = buildInstrument(rig);
        const auto trackB = rig.add("Track In", &uuidB);
        oscB = rig.add("Oscillator");
        rig.tracks = {{uuidA, "Bass"}, {uuidB, "Lead"}};
        rig.midi(trackA, a.osc);
        rig.midi(trackB, oscB);
        rig.wire(oscB, 0, a.filter, 0);
    }
};
} // namespace

TEST_F(GraphEditorTest, ACableBetweenTwoTracksAsksAndThisTrackOnlyLeavesTheOtherMono) {
    TwoTrackRig t;
    t.rig.answer = PolyChainChoice::thisTrackOnly;

    t.rig.clickPill(t.a.osc);

    ASSERT_EQ(t.rig.prompts.size(), 1u);
    EXPECT_EQ(t.rig.prompts[0].message, "This also changes 1 module on Lead.");
    EXPECT_TRUE(t.rig.isPoly(t.a.osc));
    EXPECT_FALSE(t.rig.isPoly(t.oscB));
}

TEST_F(GraphEditorTest, IncludeConnectedTracksSwitchesTheOtherTrackToo) {
    TwoTrackRig t;
    t.rig.answer = PolyChainChoice::includeConnectedTracks;

    t.rig.clickPill(t.a.osc);

    EXPECT_TRUE(t.rig.isPoly(t.a.osc));
    EXPECT_TRUE(t.rig.isPoly(t.oscB));
}

TEST_F(GraphEditorTest, CancellingTheQuestionChangesNothingAndLeavesNoUndoStep) {
    TwoTrackRig t;
    t.rig.answer = PolyChainChoice::cancel;
    const auto before = t.rig.cables();

    t.rig.clickPill(t.a.osc);

    EXPECT_FALSE(t.rig.isPoly(t.a.osc));
    EXPECT_FALSE(t.rig.pill(t.a.osc)->getToggleState());
    EXPECT_EQ(t.rig.cables(), before);
    EXPECT_FALSE(t.rig.undo.getUndoManager().canUndo());
    EXPECT_TRUE(t.rig.toasts.empty());
}

TEST_F(GraphEditorTest, OneUndoRestoresEverythingAndRedoReappliesIt) {
    ChainRig rig;
    const auto i = buildInstrument(rig);
    const auto before = rig.cables();

    rig.clickPill(i.osc);
    const auto after = rig.cables();
    ASSERT_NE(before, after);
    ASSERT_TRUE(rig.undo.getUndoManager().canUndo());

    rig.undo.undo();
    EXPECT_EQ(rig.cables(), before);
    EXPECT_TRUE(rig.polyMidis().empty());
    for (const auto id : {i.osc, i.filter, i.adsr, i.vca})
        EXPECT_FALSE(rig.isPoly(id));
    EXPECT_FALSE(rig.undo.getUndoManager().canUndo()) << "the click was ONE step, not a step plus a half";

    rig.undo.redo();
    EXPECT_EQ(rig.cables(), after);
    for (const auto id : {i.osc, i.filter, i.adsr, i.vca})
        EXPECT_TRUE(rig.isPoly(id));
    EXPECT_EQ(rig.polyMidis().size(), 1u);
}

TEST_F(GraphEditorTest, PolyOffRestoresRawMidiRemovesTheOrphanPolyMidiAndANoteStillSounds) {
    ChainRig rig;
    const auto i = buildInstrument(rig);
    rig.clickPill(i.osc);
    ASSERT_EQ(rig.polyMidis().size(), 1u);

    rig.clickPill(i.vca);

    for (const auto id : {i.osc, i.filter, i.adsr, i.vca})
        EXPECT_FALSE(rig.isPoly(id));
    EXPECT_TRUE(rig.polyMidis().empty());
    EXPECT_TRUE(rig.graph().isConnected({{i.source, kMidi}, {i.osc, kMidi}}));
    EXPECT_TRUE(rig.graph().isConnected({{i.source, kMidi}, {i.adsr, kMidi}}));
    EXPECT_EQ(rig.toasts.back(), "Made 4 modules mono");
    EXPECT_GT(playChord(rig, {60}), 0.0f);
}

TEST_F(GraphEditorTest, TheToastUndoActionUndoesTheStepOnlyWhileItIsStillTheLastEdit) {
    ChainRig rig;
    const auto i = buildInstrument(rig);
    rig.clickPill(i.osc);
    ASSERT_TRUE(rig.toastAction != nullptr);

    auto action = rig.toastAction;
    rig.clickPill(i.osc); // switching back is a later edit
    action();             // the stale Undo must not undo that
    EXPECT_FALSE(rig.isPoly(i.osc));
    EXPECT_TRUE(rig.undo.getUndoManager().canUndo());

    rig.clickPill(i.osc);
    ASSERT_TRUE(rig.toastAction != nullptr);
    rig.toastAction();
    EXPECT_FALSE(rig.isPoly(i.osc));
    EXPECT_TRUE(rig.polyMidis().empty());
}

TEST_F(GraphEditorTest, AClickThatChangesOnlyTheClickedModuleShowsNoToast) {
    ChainRig rig;
    const auto osc = rig.add("Oscillator");
    rig.clickPill(osc);
    EXPECT_TRUE(rig.isPoly(osc));
    EXPECT_TRUE(rig.toasts.empty());
}

TEST_F(GraphEditorTest, AnUnconnectedFilterJoinsAPolyOscillatorWhenCabledButAConnectedOneDoesNot) {
    ChainRig rig;
    const auto osc = rig.add("Oscillator");
    rig.setPoly(osc, true);
    const auto fresh = rig.add("Filter");
    const auto connected = rig.add("Filter");
    const auto vca = rig.add("VCA");
    rig.editor.connectPorts(connected, 0, vca, 0, false, false); // it already has a cable

    rig.editor.connectPorts(osc, 0, fresh, 0, false, true);
    EXPECT_TRUE(rig.isPoly(fresh));
    EXPECT_GE(rig.count(osc, fresh), 8);

    rig.editor.connectPorts(osc, 0, connected, 0, false, true);
    EXPECT_FALSE(rig.isPoly(connected));

    rig.undo.undo();
    rig.undo.undo();
    EXPECT_FALSE(rig.isPoly(fresh)) << "the join is inside the cable's own undo step";
}

TEST_F(GraphEditorTest, AnUnconnectedModuleJoinsAPolyMidiItIsCabledFrom) {
    ChainRig rig;
    const auto polyMidi = rig.add("Poly MIDI");
    const auto osc = rig.add("Oscillator");
    rig.editor.connectPorts(polyMidi, 0, osc, 0, false, true);
    EXPECT_TRUE(rig.isPoly(osc));
}

TEST_F(GraphEditorTest, ProgrammaticPolySetsNeverPropagate) {
    ChainRig rig;
    const auto i = buildInstrument(rig);

    rig.setPoly(i.osc, true); // what the AI, MIDI Learn, automation and a project load do

    EXPECT_FALSE(rig.isPoly(i.filter));
    EXPECT_FALSE(rig.isPoly(i.vca));
    EXPECT_TRUE(rig.polyMidis().empty());
    EXPECT_TRUE(rig.toasts.empty());
    EXPECT_FALSE(rig.undo.getUndoManager().canUndo());

    rig.clickPill(i.filter); // a user click: the whole graph
    rig.undo.undo();         // an undo restore sets every parameter back and must not propagate again
    EXPECT_TRUE(rig.isPoly(i.osc)) << "only the click was undone";
    EXPECT_FALSE(rig.isPoly(i.filter));
    EXPECT_EQ(rig.toasts.size(), 1u);
}
