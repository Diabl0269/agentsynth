#include "../../Mixer/ChannelFlow/ChannelFlowTestRigs.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroPortFlowTestHelpers.h"
#include "Modules/ChannelStripModule.h"

// Topic: dragging a cable onto a collapsed macro card creates a port and wires it, with no
// Configure I/O dialog involved (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed convenience
// path).

// ============================================================================
// Cable-drop convenience (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed)
// ============================================================================

TEST(MacroPortFlow, DroppingACableOnACollapsedCardCreatesAMonoInputAndWiresIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_TRUE(editor.getMacros().find(macroId)->collapsed);

    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    ModuleComponent* extComp = nullptr;
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == extOsc)
            extComp = c;
    ASSERT_NE(extComp, nullptr);

    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    ASSERT_TRUE(card->isVisible());
    const auto dropPoint = card->getBounds().getCentre();

    // Drag from the external Oscillator's audio OUTPUT (jack 0) and release over the collapsed
    // card — no jack under the cursor (none drawn yet), so this exercises the
    // convenience path: an output source means the macro should gain an INPUT.
    editor.beginConnectionDrag(extComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(dropPoint);

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_TRUE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::AudioCV);

    const auto newNodeId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    EXPECT_TRUE(hasConnection(engine, extOsc, 0, newNodeId, 0)) << "the boundary connection was wired";
}

TEST(MacroPortFlow, DroppingAMidiCableOnACollapsedCardCreatesAMidiOutput) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    auto extMidi = addModuleAt(editor, engine, std::make_unique<MidiKeyboardModule>(), 900, 900);
    ModuleComponent* extComp = nullptr;
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == extMidi)
            extComp = c;
    ASSERT_NE(extComp, nullptr);

    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const auto dropPoint = card->getBounds().getCentre();

    // Dragging FROM an input jack (isInput=true) looking for a source -> the macro should offer
    // an OUTPUT. MidiKeyboardModule has no input jack, so this simulates the gesture directly via
    // the same primitive endConnectionDrag itself drives from a real jack drag.
    editor.beginConnectionDrag(extComp, 0, /*isInput=*/true, /*isMidi=*/true, {0, 0});
    editor.endConnectionDrag(dropPoint);

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_FALSE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::Midi);
}

// Topic: the cable-drop convenience (a cable released on a collapsed macro card mints a port and wires it) works for
// EVERY module type the factory can build, in both directions, not for the few the tests above pick by hand
// (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed). A sibling-gap guard: the sweep walks the
// factory, not a hand-picked list.

namespace {

// Factory types that are the macro's own plumbing, not an outside module a cable can start or end on.
bool isMacroPlumbing(const juce::String& type) {
    return type == "Macros" || type == "Macro In" || type == "Macro Out" || type == "Macro Midi In" ||
           type == "Macro Midi Out";
}

// True when a cable path leads from `a` to `b`. A cable into a CV jack runs through an attenuverter node
// (AudioEngine::addModRouting), so a direct edge is too strict.
bool anyEdgeBetween(AudioEngine& engine, NodeID a, NodeID b, bool midi) {
    std::vector<NodeID> frontier{a};
    std::vector<NodeID> seen{a};
    while (!frontier.empty()) {
        const auto at = frontier.back();
        frontier.pop_back();
        for (const auto& c : engine.getGraph().getConnections()) {
            if (c.source.nodeID != at || c.source.isMIDI() != midi)
                continue;
            if (c.destination.nodeID == b)
                return true;
            if (std::find(seen.begin(), seen.end(), c.destination.nodeID) == seen.end()) {
                seen.push_back(c.destination.nodeID);
                frontier.push_back(c.destination.nodeID);
            }
        }
    }
    return false;
}

// Runs the message queue until everything already posted has been handled. Every rig leaves deferred canvas
// updates behind that each cost a message-loop pass even once the rig is gone; a sweep builds hundreds of rigs, so
// the leftover backlog takes a few hundred milliseconds to work through and would otherwise delay the next test's
// own posted messages by that long (a test that waits a fixed 50 ms for one then fails, order-dependently).
struct DrainMessageQueueOnExit {
    ~DrainMessageQueueOnExit() {
        bool reached = false;
        juce::MessageManager::callAsync([&reached] { reached = true; });
        while (!reached)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    }
};

struct Rig {
    AudioEngine engine;
    GraphEditor editor{engine};
    juce::String macroId;
    juce::String outerMacroId;
    NodeID external;
    ModuleBase* module = nullptr;

    // False when the factory type is not a module card (a graph-IO node).
    // `insideOpenMacro`: the module is a member of a second, expanded macro (as a track's strip is), so its cable
    // leaves that macro's boundary too.
    bool build(const juce::String& type, bool insideOpenMacro = false, bool flipDualIO = false) {
        editor.setSize(1600, 1200);
        macroId = makeTwoMemberMacro(editor, engine);
        auto processor = synth::AIStateMapper::createModule(type);
        module = dynamic_cast<ModuleBase*>(processor.get());
        if (module == nullptr)
            return false;
        if (flipDualIO && module->hasDualIOParameter())
            for (auto* p : module->getParameters())
                if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p); b != nullptr && b->paramID == "dualIO")
                    *b = !b->get();
        if (flipDualIO)
            if (auto* strip = dynamic_cast<ChannelStripModule*>(module))
                strip->setShape(ChannelStripModule::Shape::Mono); // the other input shape
        external = addModuleAt(editor, engine, std::move(processor), 900, 900);
        if (insideOpenMacro) {
            auto mate = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 1100);
            editor.setSelectedNodes({external, mate});
            outerMacroId = editor.getMacroController().groupSelectionIntoMacro();
            editor.getMacroController().setMacroCollapsed(outerMacroId, false);
            editor.updateComponents();
        }
        return compFor(editor, external) != nullptr;
    }

    // One drag from a jack of the outside module, released over the collapsed card's centre.
    // Returns the new port's node (or an invalid id when no port was minted).
    NodeID dropOnCard(int jack, bool fromInput, bool midi) {
        auto* macro = editor.getMacros().find(macroId);
        const auto before = macro->ports.size();
        auto* card = editor.getMacroController().getMacroCardForTest(macroId);
        if (card == nullptr || !card->isVisible())
            return {};
        const auto point = card->localPointToGlobal(card->getLocalBounds().getCentre());
        editor.beginConnectionDrag(compFor(editor, external), jack, fromInput, midi, {0, 0});
        editor.endConnectionDrag(point);
        macro = editor.getMacros().find(macroId);
        if (macro->ports.size() != before + 1)
            return {};
        return nodeIdForUuid(engine, macro->ports.back().nodeUuid);
    }
};

} // namespace

static void sweepOutputs(bool inside, bool flip) {
    DrainMessageQueueOnExit drainOnExit;
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        if (isMacroPlumbing(type))
            continue;
        Rig probe;
        if (!probe.build(type, inside, flip))
            continue;
        std::vector<std::pair<int, bool>> jacks; // {jack, midi}
        for (int j = 0; j < probe.module->getVisibleOutputPortCount(); ++j)
            jacks.emplace_back(j, false);
        if (probe.module->producesMidi())
            jacks.emplace_back(0, true);

        for (const auto& [jack, midi] : jacks) {
            SCOPED_TRACE((type + (midi ? " MIDI out" : " out ") + juce::String(jack)).toStdString());
            Rig rig;
            ASSERT_TRUE(rig.build(type, inside, flip));
            ASSERT_TRUE(rig.editor.getMacros().find(rig.macroId)->collapsed);
            const auto portNode = rig.dropOnCard(jack, /*fromInput=*/false, midi);
            ASSERT_NE(portNode, NodeID()) << "no port was minted";
            const auto* port = &rig.editor.getMacros().find(rig.macroId)->ports.back();
            EXPECT_TRUE(port->isInput);
            EXPECT_EQ(port->kind, midi ? synth::MacroPortKind::Midi : synth::MacroPortKind::AudioCV);
            EXPECT_TRUE(anyEdgeBetween(rig.engine, rig.external, portNode, midi)) << "the cable was not wired";
        }
    }
}

static void sweepInputs(bool inside, bool flip) {
    DrainMessageQueueOnExit drainOnExit;
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        if (isMacroPlumbing(type))
            continue;
        Rig probe;
        if (!probe.build(type, inside, flip))
            continue;
        std::vector<std::pair<int, bool>> jacks; // {jack, midi}
        for (int j = 0; j < probe.module->getVisibleInputPortCount(); ++j)
            jacks.emplace_back(j, false);
        if (probe.module->acceptsMidi())
            jacks.emplace_back(0, true);

        for (const auto& [jack, midi] : jacks) {
            SCOPED_TRACE((type + (midi ? " MIDI in" : " in ") + juce::String(jack)).toStdString());
            Rig rig;
            ASSERT_TRUE(rig.build(type, inside, flip));
            const auto portNode = rig.dropOnCard(jack, /*fromInput=*/true, midi);
            ASSERT_NE(portNode, NodeID()) << "no port was minted";
            const auto* port = &rig.editor.getMacros().find(rig.macroId)->ports.back();
            EXPECT_FALSE(port->isInput);
            EXPECT_EQ(port->kind, midi ? synth::MacroPortKind::Midi : synth::MacroPortKind::AudioCV);
            EXPECT_TRUE(anyEdgeBetween(rig.engine, portNode, rig.external, midi)) << "the cable was not wired";
        }
    }
}

TEST(MacroPortFlow, EveryModuleTypeOutputDroppedOnACollapsedCardMintsAnInputPortAndWiresIt) {
    sweepOutputs(false, false);
}
TEST(MacroPortFlow, EveryModuleTypeInputDroppedOnACollapsedCardMintsAnOutputPortAndWiresIt) {
    sweepInputs(false, false);
}
TEST(MacroPortFlow, EveryModuleTypeOutputInsideAnOpenMacroDroppedOnACollapsedCardWiresThroughBothBoundaries) {
    sweepOutputs(true, false);
}
TEST(MacroPortFlow, EveryModuleTypeInputInsideAnOpenMacroDroppedOnACollapsedCardWiresThroughBothBoundaries) {
    sweepInputs(true, false);
}
TEST(MacroPortFlow, EveryModuleTypeOutputWithItsOtherJackLayoutDroppedOnACollapsedCardWires) {
    sweepOutputs(false, true);
}
TEST(MacroPortFlow, EveryModuleTypeInputWithItsOtherJackLayoutDroppedOnACollapsedCardWires) {
    sweepInputs(false, true);
}

// Topic: a cable dragged from a real track's Channel Strip (a member of the open channel macro, already cabled to the
// channel's own output port) onto a collapsed macro card mints a port and wires it, as for any other module
// (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed).

namespace {

struct ChannelRig {
    HostedPatchCFT patch;
    GraphEditor editor{patch.engine};
    juce::String targetMacroId;
    juce::AudioProcessorGraph::Node* strip = nullptr;

    bool build() {
        editor.setSize(2400, 1600);
        const auto rig = buildLegacyRigCFT(patch.engine, patch.output, RigShapeCFT::SharedLfo);
        if (!editor.makeChannelFromNode(rig.trackInA->nodeID, "Lead"))
            return false;
        const auto* channel = editor.getMacros().findByMember(nodeUuid(rig.trackInA));
        if (channel == nullptr)
            return false;
        strip = findMacroMemberOfTypeCFT(patch.engine.getGraph(), *channel, ModuleType::ChannelStrip);
        editor.getMacroController().setMacroCollapsed(channel->id, false);
        editor.updateComponents();
        targetMacroId = makeTwoMemberMacro(editor, patch.engine);
        return strip != nullptr && !targetMacroId.isEmpty();
    }

    bool reaches(NodeID from, NodeID to) {
        std::vector<NodeID> todo{from}, seen{from};
        while (!todo.empty()) {
            const auto at = todo.back();
            todo.pop_back();
            for (const auto& c : patch.engine.getGraph().getConnections()) {
                if (c.source.nodeID != at || c.source.isMIDI())
                    continue;
                if (c.destination.nodeID == to)
                    return true;
                if (std::find(seen.begin(), seen.end(), c.destination.nodeID) == seen.end()) {
                    seen.push_back(c.destination.nodeID);
                    todo.push_back(c.destination.nodeID);
                }
            }
        }
        return false;
    }

    // Returns the target macro's new port node, or an invalid id when none was minted.
    NodeID drop(int jack, bool fromInput) {
        auto* macro = editor.getMacros().find(targetMacroId);
        const auto before = macro->ports.size();
        auto* source = compFor(editor, strip->nodeID);
        auto* card = editor.getMacroController().getMacroCardForTest(targetMacroId);
        if (source == nullptr || card == nullptr || !card->isVisible())
            return {};
        editor.beginConnectionDrag(source, jack, fromInput, /*isMidi=*/false, {0, 0});
        editor.endConnectionDrag(card->localPointToGlobal(card->getLocalBounds().getCentre()));
        macro = editor.getMacros().find(targetMacroId);
        return macro->ports.size() == before + 1 ? nodeIdForUuid(patch.engine, macro->ports.back().nodeUuid) : NodeID();
    }
};

} // namespace

TEST(MacroPortFlow, EveryOutputJackOfATracksChannelStripDroppedOnACollapsedCardMintsAnInputPortAndWiresIt) {
    ChannelRig probe;
    ASSERT_TRUE(probe.build());
    const int jacks = dynamic_cast<ModuleBase*>(probe.strip->getProcessor())->getVisibleOutputPortCount();
    ASSERT_GE(jacks, 2);
    for (int jack = 0; jack < jacks; ++jack) {
        SCOPED_TRACE("Channel Strip out " + std::to_string(jack));
        ChannelRig rig;
        ASSERT_TRUE(rig.build());
        const auto portNode = rig.drop(jack, /*fromInput=*/false);
        ASSERT_NE(portNode, NodeID()) << "no port was minted";
        EXPECT_TRUE(rig.editor.getMacros().find(rig.targetMacroId)->ports.back().isInput);
        EXPECT_TRUE(rig.reaches(rig.strip->nodeID, portNode)) << "the cable was not wired";
    }
}

TEST(MacroPortFlow, EveryInputJackOfATracksChannelStripDroppedOnACollapsedCardMintsAnOutputPortAndWiresIt) {
    ChannelRig probe;
    ASSERT_TRUE(probe.build());
    const int jacks = dynamic_cast<ModuleBase*>(probe.strip->getProcessor())->getVisibleInputPortCount();
    for (int jack = 0; jack < jacks; ++jack) {
        SCOPED_TRACE("Channel Strip in " + std::to_string(jack));
        ChannelRig rig;
        ASSERT_TRUE(rig.build());
        const auto portNode = rig.drop(jack, /*fromInput=*/true);
        ASSERT_NE(portNode, NodeID()) << "no port was minted";
        EXPECT_FALSE(rig.editor.getMacros().find(rig.targetMacroId)->ports.back().isInput);
        EXPECT_TRUE(rig.reaches(portNode, rig.strip->nodeID)) << "the cable was not wired";
    }
}

TEST(MacroPortFlow, ATracksChannelStripCableDroppedOnTheCardsExistingPortJackReusesThatPort) {
    ChannelRig rig;
    ASSERT_TRUE(rig.build());
    const auto portNode = rig.drop(0, /*fromInput=*/false);
    ASSERT_NE(portNode, NodeID());
    auto* card = rig.editor.getMacroController().getMacroCardForTest(rig.targetMacroId);
    const auto layout = rig.editor.getMacroController().macroCardPortLayout(rig.targetMacroId);
    ASSERT_EQ(layout.size(), 1u);

    auto* source = compFor(rig.editor, rig.strip->nodeID);
    ASSERT_NE(source, nullptr);
    rig.editor.beginConnectionDrag(source, 1, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    rig.editor.endConnectionDrag(card->localPointToGlobal(layout[0].jackPos));

    EXPECT_EQ(rig.editor.getMacros().find(rig.targetMacroId)->ports.size(), 1u) << "no second port was minted";
    EXPECT_TRUE(rig.reaches(rig.strip->nodeID, portNode));
}

TEST(MacroPortFlow, ATracksChannelStripInputCableDroppedOnTheCardsExistingOutputPortJackReusesThatPort) {
    ChannelRig rig;
    ASSERT_TRUE(rig.build());
    const auto portNode = rig.drop(0, /*fromInput=*/true);
    ASSERT_NE(portNode, NodeID());
    auto* card = rig.editor.getMacroController().getMacroCardForTest(rig.targetMacroId);
    const auto layout = rig.editor.getMacroController().macroCardPortLayout(rig.targetMacroId);
    ASSERT_EQ(layout.size(), 1u);

    auto* source = compFor(rig.editor, rig.strip->nodeID);
    ASSERT_NE(source, nullptr);
    rig.editor.beginConnectionDrag(source, 0, /*isInput=*/true, /*isMidi=*/false, {0, 0});
    rig.editor.endConnectionDrag(card->localPointToGlobal(layout[0].jackPos));

    EXPECT_EQ(rig.editor.getMacros().find(rig.targetMacroId)->ports.size(), 1u) << "no second port was minted";
    EXPECT_TRUE(rig.reaches(portNode, rig.strip->nodeID));
}

// A jack of the wrong kind passed over on the release is no refusal once the collapsed card under it took the cable:
// the status bar stays silent.
TEST(MacroPortFlow, ACardThatTakesTheDropLeavesNoRefusalFromAJackUnderIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    auto source = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    auto blocker = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1200, 900);
    ModuleComponent* sourceComp = nullptr;
    ModuleComponent* blockerComp = nullptr;
    for (auto* c : editor.getModuleComponents()) {
        if (c != nullptr && c->getNodeId() == source)
            sourceComp = c;
        if (c != nullptr && c->getNodeId() == blocker)
            blockerComp = c;
    }
    ASSERT_NE(sourceComp, nullptr);
    ASSERT_NE(blockerComp, nullptr);
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    const auto dropPoint = card->getBounds().getCentre();
    // The blocker's MIDI input jack sits on the card's centre, above the card: an audio cable there is refused by the
    // jack, and the card beneath takes it.
    blockerComp->setTopLeftPosition(dropPoint - blockerComp->getMidiPortCenter(false));
    ASSERT_TRUE(blockerComp->getPortForPoint(dropPoint - blockerComp->getPosition()).has_value());

    juce::String status;
    editor.onStatusMessage = [&status](const juce::String& msg) { status = msg; };
    editor.beginConnectionDrag(sourceComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(dropPoint);

    EXPECT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u) << "the card took the cable";
    EXPECT_TRUE(status.isEmpty()) << status;
}
