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
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/GraphEditor/GraphEditorCableCurveChips.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/PolyChain/PolyChainController.h"
#include "UI/Layout/ReducedMotion.h"
#include <bit>
#include <memory>
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
        node->properties.set("x", x += 440); // the Oscillator is 408 wide
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
        // Button::triggerClick only POSTS the click; a fixed 20 ms pump is not enough on a loaded CI runner (messages
        // queued by earlier tests run first), so pump until the click has actually been delivered.
        auto delivered = std::make_shared<bool>(false);
        button->onClick = [delivered, original = button->onClick] {
            *delivered = true;
            if (original)
                original();
        };
        button->triggerClick();
        const auto deadline = juce::Time::getMillisecondCounter() + 5000;
        while (!*delivered && juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        ASSERT_TRUE(*delivered) << "the Poly pill click was never delivered";
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

void put(ChainRig& rig, NodeID id, int x, int y) {
    auto* node = rig.graph().getNodeForId(id);
    node->properties.set("x", x);
    node->properties.set("y", y);
}

juce::Rectangle<int> cardRect(ChainRig& rig, NodeID id) {
    auto* comp = findModuleComp(rig.editor, rig.proc(id));
    return comp != nullptr ? comp->getBounds() : juce::Rectangle<int>();
}

// Room for Poly MIDI between the source and the Oscillator, but its natural spot (a layer gap left of that card) is
// left of the source's column, which is where it used to land.
Instrument buildRoomyInstrument(ChainRig& rig) {
    auto i = buildInstrument(rig);
    put(rig, i.source, 400, 100);
    put(rig, i.osc, 720, 100);
    put(rig, i.filter, 1100, 100);
    put(rig, i.adsr, 720, 900); // cards are tall: nothing overlaps another before the click
    put(rig, i.vca, 1500, 100);
    put(rig, i.out, 1900, 100);
    rig.editor.updateComponents();
    return i;
}

} // namespace

TEST_F(GraphEditorTest, PolyOnAnOscillatorInsideAMacroLeavesTheMacroTheSameSize) {
    ChainRig rig;
    const auto i = buildRoomyInstrument(rig);
    rig.editor.setSelectedNodes({i.source, i.osc, i.filter, i.adsr, i.vca});
    const auto macroId = rig.editor.getMacroController().groupSelectionIntoMacro(false);
    ASSERT_FALSE(macroId.isEmpty());
    rig.editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto before = rig.editor.getMacroController().macroHullBounds(macroId);
    ASSERT_FALSE(before.isEmpty());

    rig.clickPill(i.osc);

    rig.editor.finishCardGlideForTest(); // read the landed cards, not mid-glide ones
    rig.editor.finishHullGlideForTest();
    ASSERT_EQ(rig.polyMidis().size(), 1u);
    const auto polyMidi = rig.polyMidis().front();
    const auto uuid = rig.graph().getNodeForId(polyMidi)->properties["uuid"].toString();
    ASSERT_NE(rig.editor.getMacros().findByMember(uuid), nullptr) << "Poly MIDI joins the macro its Oscillator is in";
    EXPECT_EQ(rig.editor.getMacroController().macroHullBounds(macroId), before) << "the macro does not grow";
    const auto card = cardRect(rig, polyMidi);
    EXPECT_TRUE(before.contains(card)) << "Poly MIDI sits inside the macro";
    for (const auto id : {i.source, i.osc, i.filter, i.adsr, i.vca})
        EXPECT_FALSE(card.intersects(cardRect(rig, id)));
    EXPECT_LE(card.getRight(), cardRect(rig, i.osc).getX()) << "before the Oscillator it feeds";
}

namespace {

// The landed state of the macro after one step of a toggle sequence: its border and where the "xN" pill of the poly bus
// is drawn (empty when the bus is not drawn).
struct Landed {
    juce::Rectangle<int> hull;
    juce::Rectangle<float> badge;
    std::vector<juce::Rectangle<int>> cards;
};

Landed landed(ChainRig& rig, const juce::String& macroId) {
    rig.editor.finishCardGlideForTest(); // read the landed cards, not mid-glide ones
    rig.editor.finishHullGlideForTest();
    rig.editor.timerCallback();
    Landed out;
    out.hull = rig.editor.getMacroController().macroHullBounds(macroId);
    for (auto* comp : rig.editor.getModuleComponents())
        out.cards.push_back(comp->getBounds());
    for (const auto& cable : rig.editor.buildVisibleCables())
        if (cable.isPolyBus && cable.voiceCount > 1) {
            const auto badge = "x" + juce::String(cable.voiceCount);
            const float width =
                (float)juce::GlyphArrangement::getStringWidth(juce::Font(juce::FontOptions(11.0f)), badge) + 8.0f;
            out.badge = synth::ui::cable_curve_chips::polyBadgeRect(cable, width, out.cards);
        }
    return out;
}

void expectBadgeWhole(const Landed& state, const juce::String& step) {
    EXPECT_FALSE(state.badge.isEmpty()) << step << ": the poly bus is drawn with its x8 pill";
    for (const auto& card : state.cards)
        EXPECT_FALSE(state.badge.intersects(card.toFloat())) << step << ": the whole x8 shows, no card over it";
}

// Poly, Mono, Poly, then undo and redo of the second Poly: the border is where it was at every landed step and the pill
// is never under a card.
void expectPolyToggleKeepsTheMacro(ChainRig& rig, const Instrument& i, bool borderKeptFromTheStart) {
    rig.editor.setSelectedNodes({i.source, i.osc, i.filter, i.adsr, i.vca});
    const auto macroId = rig.editor.getMacroController().groupSelectionIntoMacro(false);
    ASSERT_FALSE(macroId.isEmpty());
    rig.editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto start = landed(rig, macroId);
    ASSERT_FALSE(start.hull.isEmpty());

    rig.clickPill(i.osc);
    const auto poly1 = landed(rig, macroId);
    expectBadgeWhole(poly1, "Poly");
    if (borderKeptFromTheStart)
        EXPECT_EQ(poly1.hull, start.hull) << "Poly";

    rig.clickPill(i.osc);
    const auto mono = landed(rig, macroId);
    EXPECT_EQ(mono.hull, start.hull) << "Mono returns the border to where it was";

    rig.clickPill(i.osc);
    const auto poly2 = landed(rig, macroId);
    expectBadgeWhole(poly2, "Poly again");
    EXPECT_EQ(poly2.hull, poly1.hull) << "Poly again";
    EXPECT_EQ(poly2.cards, poly1.cards) << "Poly again puts every card where the first Poly did";

    ASSERT_TRUE(rig.undo.undo());
    EXPECT_EQ(landed(rig, macroId).hull, mono.hull) << "undo of the second Poly";
    ASSERT_TRUE(rig.undo.redo());
    const auto redone = landed(rig, macroId);
    expectBadgeWhole(redone, "redo");
    EXPECT_EQ(redone.hull, poly2.hull) << "redo";
}

} // namespace

TEST_F(GraphEditorTest, PolyMonoPolyUndoRedoOnARoomyMacroKeepsTheBorderAndTheWholeBadge) {
    ChainRig rig;
    const auto i = buildRoomyInstrument(rig);
    expectPolyToggleKeepsTheMacro(rig, i, /*borderKeptFromTheStart=*/true);
}

// Cards packed the way a freshly added track lays them out: no spare room for Poly MIDI beside its source.
TEST_F(GraphEditorTest, PolyMonoPolyUndoRedoOnAPackedMacroKeepsTheBorderAndTheWholeBadge) {
    ChainRig rig;
    const auto i = buildInstrument(rig);
    expectPolyToggleKeepsTheMacro(rig, i, /*borderKeptFromTheStart=*/false);
}

TEST_F(GraphEditorTest, PolyOutsideAnyMacroLandsBesideItsSourceNotAtTheCanvasEdge) {
    ChainRig rig;
    const auto i = buildInstrument(rig);
    put(rig, i.source, 100, 100);
    put(rig, i.osc, 800, 100);
    rig.editor.updateComponents();

    rig.clickPill(i.osc);

    ASSERT_EQ(rig.polyMidis().size(), 1u);
    const auto card = cardRect(rig, rig.polyMidis().front());
    EXPECT_GE(card.getX(), cardRect(rig, i.source).getRight()) << "right of its source";
    EXPECT_LE(card.getRight(), cardRect(rig, i.osc).getX()) << "left of the Oscillator it feeds";
    EXPECT_NEAR(card.getY(), cardRect(rig, i.osc).getY(), 16)
        << "on the Oscillator's row, not walked down below the source";
    EXPECT_FALSE(card.intersects(cardRect(rig, i.source)));
}

TEST_F(GraphEditorTest, ThePolyVoiceBadgeSlidesOffACardCoveringTheCableMidpoint) {
    ChainRig rig;
    const auto i = buildInstrument(rig);
    rig.clickPill(i.osc);
    ASSERT_EQ(rig.polyMidis().size(), 1u);
    // A long wire: Poly MIDI far left of the Oscillator, every other card out of its way.
    put(rig, rig.polyMidis().front(), 100, 100);
    put(rig, i.osc, 1500, 100);
    put(rig, i.source, 100, 700);
    put(rig, i.adsr, 700, 700);
    put(rig, i.vca, 1700, 700);
    put(rig, i.out, 2300, 700);
    rig.editor.updateComponents();
    rig.editor.timerCallback();

    auto polyBus = [&]() -> std::optional<GraphEditor::VisibleCable> {
        for (const auto& cable : rig.editor.buildVisibleCables())
            if (cable.isPolyBus && cable.voiceCount > 1)
                return cable;
        return std::nullopt;
    };
    auto cable = polyBus();
    ASSERT_TRUE(cable.has_value()) << "the Poly MIDI -> Oscillator bus is drawn as one wire";
    const auto mid = (cable->p1 + cable->p2) / 2.0f;
    put(rig, i.filter, (int)mid.x - 100, (int)mid.y - 40); // a card right under the wire's midpoint
    rig.editor.updateComponents();
    rig.editor.timerCallback();
    cable = polyBus();
    ASSERT_TRUE(cable.has_value());
    ASSERT_TRUE(cardRect(rig, i.filter).toFloat().contains((cable->p1 + cable->p2) / 2.0f));

    std::vector<juce::Rectangle<int>> cards;
    for (auto* comp : rig.editor.getModuleComponents())
        cards.push_back(comp->getBounds());
    const auto pill = synth::ui::cable_curve_chips::polyBadgeRect(*cable, 30.0f, cards);
    for (auto* comp : rig.editor.getModuleComponents())
        EXPECT_FALSE(pill.intersects(comp->getBounds().toFloat()))
            << "the whole badge shows; pill " << pill.toString() << " card " << comp->getBounds().toString() << " mid "
            << ((cable->p1 + cable->p2) / 2.0f).toString();
}

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
    EXPECT_EQ(rig.count(pm, osc2), 16) << "the Pitch fan and the Gate fan";
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

// The track header's Mono | Poly switch drives the same flow from the track's source node.
TEST_F(GraphEditorTest, TheTrackSwitchReadsNoneMonoMixedAndPolyFromTheVoiceGraph) {
    ChainRig rig;
    const auto lonely = rig.add("Midi Input");
    EXPECT_EQ(rig.editor.getPolyChain().trackPolyState(lonely), synth::ui::TrackPolyState::none);

    const auto i = buildInstrument(rig);
    auto& chain = rig.editor.getPolyChain();
    EXPECT_EQ(chain.trackPolyState(i.source), synth::ui::TrackPolyState::mono);
    rig.setPoly(i.osc, true);
    EXPECT_EQ(chain.trackPolyState(i.source), synth::ui::TrackPolyState::mixed);
    for (const auto id : {i.filter, i.adsr, i.vca})
        rig.setPoly(id, true);
    EXPECT_EQ(chain.trackPolyState(i.source), synth::ui::TrackPolyState::poly);
}

TEST_F(GraphEditorTest, TheTrackSwitchMakesTheWholeChainPolyAsOneUndoStepAndAChordSoundsOnSeveralVoices) {
    ChainRig rig;
    const auto i = buildInstrument(rig);
    int changed = 0;
    rig.editor.getPolyChain().onVoiceGraphChanged = [&] { ++changed; };

    rig.editor.getPolyChain().trackPolyClicked(i.source, true);

    for (const auto id : {i.osc, i.filter, i.adsr, i.vca})
        EXPECT_TRUE(rig.isPoly(id));
    EXPECT_EQ(rig.polyMidis().size(), 1u);
    EXPECT_EQ(rig.editor.getPolyChain().trackPolyState(i.source), synth::ui::TrackPolyState::poly);
    EXPECT_EQ(changed, 1);
    EXPECT_GT(playChord(rig, {60, 64, 67}), 0.0f);
    EXPECT_EQ(heldVoices(rig), 3);

    rig.undo.undo();
    for (const auto id : {i.osc, i.filter, i.adsr, i.vca})
        EXPECT_FALSE(rig.isPoly(id)) << "one undo step puts the whole chain back";
    EXPECT_TRUE(rig.polyMidis().empty());
}

TEST_F(GraphEditorTest, TheTrackSwitchSetsAMixedChainToMonoAndLeavesTheSourceAlone) {
    ChainRig rig;
    const auto i = buildInstrument(rig);
    rig.setPoly(i.osc, true);
    rig.setPoly(i.vca, true);

    rig.editor.getPolyChain().trackPolyClicked(i.source, false);

    for (const auto id : {i.osc, i.filter, i.adsr, i.vca})
        EXPECT_FALSE(rig.isPoly(id));
    EXPECT_EQ(rig.editor.getPolyChain().trackPolyState(i.source), synth::ui::TrackPolyState::mono);
}

// A Poly click moves the canvas the way Cmd+Z of it does (docs/layout/undo-glide.md): the Poly MIDI card grows
// in, the cables grow out of their jacks, and a Mono click shrinks the card away and pulls the cables back.
namespace {
struct FullMotion {
    FullMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
        synth::ui::setReducedMotionForTest(false);
    }
    ~FullMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};
} // namespace

TEST_F(GraphEditorTest, ClickingPolyGrowsThePolyMidiCardInAndClickingMonoShrinksItAway) {
    FullMotion motion;
    ChainRig rig;
    const auto i = buildInstrument(rig);
    auto& glide = rig.editor.getCardGlideForTest();
    glide.setForceAnimateForTest(true); // not on screen: the glide is what is under test

    rig.clickPill(i.osc);

    ASSERT_EQ(rig.polyMidis().size(), 1u);
    EXPECT_GE(glide.enterGhostCount(), 1) << "the Poly MIDI card grows in";
    const auto polyMidi = rig.polyMidis().front();
    rig.editor.finishCardGlideForTest();
    EXPECT_EQ(glide.enterGhostCount(), 0);
    ASSERT_NE(findModuleComp(rig.editor, rig.proc(polyMidi)), nullptr);

    rig.clickPill(i.vca);

    EXPECT_TRUE(rig.polyMidis().empty());
    EXPECT_GE(glide.exitGhostCount(), 1) << "the Poly MIDI card shrinks away rather than vanishing";
    rig.editor.finishCardGlideForTest();
    EXPECT_EQ(glide.exitGhostCount(), 0);
}

TEST_F(GraphEditorTest, TheTrackHeaderPolySwitchGlidesToo) {
    FullMotion motion;
    ChainRig rig;
    const auto i = buildInstrument(rig);
    auto& glide = rig.editor.getCardGlideForTest();
    glide.setForceAnimateForTest(true);

    rig.editor.getPolyChain().trackPolyClicked(i.source, true);
    EXPECT_GE(glide.enterGhostCount(), 1);
    rig.editor.finishCardGlideForTest();

    rig.editor.getPolyChain().trackPolyClicked(i.source, false);
    EXPECT_GE(glide.exitGhostCount(), 1);
}

TEST_F(GraphEditorTest, ThePolyClickIsInstantWithAnimationsOff) {
    FullMotion motion;
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    ChainRig rig;
    const auto i = buildInstrument(rig);
    rig.editor.getCardGlideForTest().setForceAnimateForTest(true);

    rig.clickPill(i.osc);

    EXPECT_EQ(rig.polyMidis().size(), 1u);
    EXPECT_EQ(rig.editor.getCardGlideForTest().enterGhostCount(), 0);
    EXPECT_FALSE(rig.editor.getCardGlideForTest().isLive());
}

// One undo step restores the cable the click took away.
TEST_F(GraphEditorTest, ThePolyClickDropsTheRawMidiCableIntoTheAdsrAndOneUndoBringsItBack) {
    ChainRig rig;
    const auto i = buildInstrument(rig);
    ASSERT_EQ(rig.count(i.source, i.adsr), 1);

    rig.clickPill(i.osc);
    EXPECT_EQ(rig.count(i.source, i.adsr), 0) << "Poly MIDI feeds the poly ADSR, the raw cable is redundant";
    EXPECT_EQ(rig.count(i.source, i.osc), 1) << "a poly oscillator still reads raw MIDI on voice 0";

    ASSERT_TRUE(rig.undo.undo());
    EXPECT_EQ(rig.count(i.source, i.adsr), 1);
    EXPECT_FALSE(rig.undo.getUndoManager().canUndo()) << "the click and its cable were one step";
}

// Every output sample of a chord with a release, through the Hosted engine.
static std::vector<float> renderChordSamples(ChainRig& rig) {
    rig.engine.prepareForHost(44100.0, 512, 2, 2);
    std::vector<float> samples;
    for (int block = 0; block < 24; ++block) {
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        if (block == 0)
            for (const int note : {60, 64, 67})
                midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 40);
        if (block == 3)
            midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.6f), 100);
        if (block == 12)
            for (const int note : {60, 64, 67, 72})
                midi.addEvent(juce::MidiMessage::noteOff(1, note), 7);
        rig.engine.processHostBlock(buffer, midi);
        samples.insert(samples.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + 512);
    }
    rig.engine.releaseFromHost();
    return samples;
}

// The proof the raw MIDI cable into a poly ADSR is redundant: the same chord renders sample for sample the same with
// and without it.
TEST_F(GraphEditorTest, APolyAdsrRendersTheSameWithAndWithoutItsRawMidiCable) {
    ChainRig with;
    const auto a = buildInstrument(with);
    with.clickPill(a.osc);
    ASSERT_EQ(with.count(a.source, a.adsr), 0);
    with.midi(a.source, a.adsr); // put the raw cable back

    ChainRig without;
    const auto b = buildInstrument(without);
    without.clickPill(b.osc);
    ASSERT_EQ(without.count(b.source, b.adsr), 0);

    const auto x = renderChordSamples(with);
    const auto y = renderChordSamples(without);
    ASSERT_EQ(x.size(), y.size());
    float peak = 0.0f;
    for (size_t n = 0; n < x.size(); ++n) {
        ASSERT_EQ(x[n], y[n]) << "sample " << n;
        peak = std::max(peak, std::abs(x[n]));
    }
    EXPECT_GT(peak, 0.001f) << "the chord is audible, so the comparison means something";
}

namespace {
struct Ev {
    int block;
    int pos;
    bool on;
    int note;
};

std::vector<float> renderStream(ChainRig& rig, const std::vector<Ev>& stream) {
    rig.engine.prepareForHost(44100.0, 512, 2, 2);
    std::vector<float> samples;
    for (int block = 0; block < 30; ++block) {
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        for (const auto& e : stream)
            if (e.block == block)
                midi.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, 0.8f) : juce::MidiMessage::noteOff(1, e.note),
                              e.pos);
        rig.engine.processHostBlock(buffer, midi);
        samples.insert(samples.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + 512);
    }
    rig.engine.releaseFromHost();
    return samples;
}
} // namespace

// The raw MIDI cable into a poly Oscillator is NOT redundant when Poly MIDI feeds its pitch and gate fans: voice 0 also
// restarts on a raw note-on and keeps a raw pitch, so the render differs without it for every stream below except a
// lone note. That is why Poly keeps the Oscillator's raw cable and removes only a poly ADSR's.
TEST_F(GraphEditorTest, ARawMidiCableIntoAPolyOscillatorStillChangesWhatPlaysWhenPolyMidiFeedsIt) {
    const std::vector<std::pair<const char*, std::vector<Ev>>> streams{
        {"single note", {{0, 40, true, 60}, {10, 0, false, 60}}},
        {"overlapping chord",
         {{0, 10, true, 60},
          {1, 300, true, 64},
          {2, 0, true, 67},
          {12, 5, false, 60},
          {13, 5, false, 64},
          {14, 5, false, 67}}},
        {"retrigger same note, off then on in one block",
         {{0, 0, true, 60}, {6, 100, false, 60}, {6, 100, true, 60}, {14, 0, false, 60}}},
        {"same note struck again while held", {{0, 0, true, 60}, {6, 200, true, 60}, {14, 0, false, 60}}},
        {"note arriving mid-block after silence",
         {{0, 0, true, 60}, {3, 0, false, 60}, {8, 333, true, 67}, {16, 0, false, 67}}},
        {"nine notes (voice steal)",
         {{0, 0, true, 40},
          {0, 1, true, 44},
          {0, 2, true, 47},
          {0, 3, true, 50},
          {0, 4, true, 52},
          {0, 5, true, 55},
          {0, 6, true, 57},
          {0, 7, true, 59},
          {1, 0, true, 62},
          {10, 0, false, 40}}},
    };
    for (const auto& [name, stream] : streams) {
        SCOPED_TRACE(name);
        ChainRig with;
        const auto a = buildInstrument(with);
        with.clickPill(a.osc);
        ASSERT_EQ(with.count(a.source, a.osc), 1);
        ChainRig without;
        const auto b = buildInstrument(without);
        without.clickPill(b.osc);
        without.graph().removeConnection({{b.source, kMidi}, {b.osc, kMidi}});
        ASSERT_EQ(without.count(b.source, b.osc), 0);

        const auto x = renderStream(with, stream);
        const auto y = renderStream(without, stream);
        ASSERT_EQ(x.size(), y.size());
        float peak = 0.0f;
        int differing = 0;
        size_t first = 0;
        for (size_t n = 0; n < x.size(); ++n) {
            peak = std::max(peak, std::abs(x[n]));
            if (x[n] != y[n] && differing++ == 0)
                first = n;
        }
        EXPECT_GT(peak, 0.001f);
        if (juce::String(name) == "single note")
            EXPECT_EQ(differing, 0) << "a lone note is the same either way";
        else
            EXPECT_GT(differing, 0) << "the raw cable changes the render (first would be at " << first << ")";
    }
}

// Regression test for FRO1981: with the cards packed (no spare room, so Poly MIDI is stacked under Track In), turning
// Poly on leaves the open macro's outline the same size, and the two input cards are exactly as wide as each other.
TEST_F(GraphEditorTest, PolyMidiUnderTrackInIsTheSameWidthAndTheMacroKeepsItsSize) {
    ChainRig rig;
    const auto i = buildInstrument(rig);
    rig.editor.setSelectedNodes({i.source, i.osc, i.filter, i.adsr, i.vca});
    const auto macroId = rig.editor.getMacroController().groupSelectionIntoMacro(false);
    ASSERT_FALSE(macroId.isEmpty());
    rig.editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto before = rig.editor.getMacroController().macroHullBounds(macroId);
    ASSERT_FALSE(before.isEmpty());

    rig.clickPill(i.osc);
    rig.editor.finishCardGlideForTest();
    rig.editor.finishHullGlideForTest();

    ASSERT_EQ(rig.polyMidis().size(), 1u);
    EXPECT_EQ(cardRect(rig, rig.polyMidis().front()).getWidth(), cardRect(rig, i.source).getWidth());
    EXPECT_EQ(rig.editor.getMacroController().macroHullBounds(macroId), before);
}

// Regression test for FRO1981: cards dragged by hand sit off the 8px grid; Poly MIDI is placed on the grid, so it must
// still line up with Track In and not widen or shorten the open macro by the few pixels it was rounded away.
TEST_F(GraphEditorTest, PolyOnOffGridCardsInAMacroLeavesTheMacroOutlineUnchanged) {
    for (const int offset : {1, 3, 5, 7}) {
        ChainRig rig;
        const auto i = buildInstrument(rig);
        rig.editor.updateComponents();
        for (const auto id : {i.source, i.osc, i.filter, i.adsr, i.vca}) {
            auto* node = rig.graph().getNodeForId(id);
            put(rig, id, static_cast<int>(node->properties["x"]) + offset,
                static_cast<int>(node->properties["y"]) + offset);
        }
        rig.editor.updateComponents();
        rig.editor.setSelectedNodes({i.source, i.osc, i.filter, i.adsr, i.vca});
        const auto macroId = rig.editor.getMacroController().groupSelectionIntoMacro(false);
        ASSERT_FALSE(macroId.isEmpty());
        rig.editor.getMacroController().setMacroCollapsed(macroId, false);
        const auto before = rig.editor.getMacroController().macroHullBounds(macroId);

        rig.clickPill(i.osc);
        rig.editor.finishCardGlideForTest();
        rig.editor.finishHullGlideForTest();

        ASSERT_EQ(rig.polyMidis().size(), 1u);
        EXPECT_EQ(rig.editor.getMacroController().macroHullBounds(macroId), before) << "offset " << offset;
        rig.clickPill(i.osc);
        rig.editor.finishCardGlideForTest();
        rig.editor.finishHullGlideForTest();
        EXPECT_EQ(rig.editor.getMacroController().macroHullBounds(macroId), before) << "poly off, offset " << offset;
    }
}
