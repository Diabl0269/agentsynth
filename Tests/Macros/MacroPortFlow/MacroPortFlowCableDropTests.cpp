#include "../../Mixer/ChannelFlow/ChannelFlowTestRigs.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroPortFlowTestHelpers.h"
#include "MacroSet.h"
#include "Modules/ChannelStripModule.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "TestPaths.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"

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

// first-of-many guard: the same jack dropped on the card a second time finds the port the first drop made
// (and the port it made on the macro the module sits in), so nothing is minted or cabled twice. Every module type,
// every jack, both directions, bare and inside an open macro.
static void sweepSameJackTwice(bool fromInput, bool inside) {
    DrainMessageQueueOnExit drainOnExit;
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        if (isMacroPlumbing(type))
            continue;
        Rig probe;
        if (!probe.build(type, inside))
            continue;
        std::vector<std::pair<int, bool>> jacks; // {jack, midi}
        const int count =
            fromInput ? probe.module->getVisibleInputPortCount() : probe.module->getVisibleOutputPortCount();
        for (int j = 0; j < count; ++j)
            jacks.emplace_back(j, false);
        if (fromInput ? probe.module->acceptsMidi() : probe.module->producesMidi())
            jacks.emplace_back(0, true);

        for (const auto& [jack, midi] : jacks) {
            SCOPED_TRACE((type + (midi ? " MIDI jack " : " jack ") + juce::String(jack)).toStdString());
            Rig rig;
            ASSERT_TRUE(rig.build(type, inside));
            ASSERT_NE(rig.dropOnCard(jack, fromInput, midi), NodeID()) << "no port was minted";
            const auto portsOf = [&rig](const juce::String& id) {
                const auto* m = id.isEmpty() ? nullptr : rig.editor.getMacros().find(id);
                return m != nullptr ? m->ports.size() : size_t(0);
            };
            const auto nodes = rig.engine.getGraph().getNumNodes();
            const auto cables = rig.engine.getGraph().getConnections().size();
            const auto outerPorts = portsOf(rig.outerMacroId);

            EXPECT_EQ(rig.dropOnCard(jack, fromInput, midi), NodeID()) << "a second port was minted";
            EXPECT_EQ(portsOf(rig.macroId), 1u);
            EXPECT_EQ(portsOf(rig.outerMacroId), outerPorts) << "the module's own macro gained a second port";
            EXPECT_EQ(rig.engine.getGraph().getNumNodes(), nodes);
            EXPECT_EQ(rig.engine.getGraph().getConnections().size(), cables) << "a cable was made twice";
        }
    }
}

TEST(MacroPortFlow, EveryModuleTypeOutputDroppedTwiceOnACollapsedCardMakesOnePort) { sweepSameJackTwice(false, false); }
TEST(MacroPortFlow, EveryModuleTypeInputDroppedTwiceOnACollapsedCardMakesOnePort) { sweepSameJackTwice(true, false); }
TEST(MacroPortFlow, EveryModuleTypeOutputInsideAnOpenMacroDroppedTwiceOnACollapsedCardMakesOnePortPerMacro) {
    sweepSameJackTwice(false, true);
}
TEST(MacroPortFlow, EveryModuleTypeInputInsideAnOpenMacroDroppedTwiceOnACollapsedCardMakesOnePortPerMacro) {
    sweepSameJackTwice(true, true);
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

// ---- Routing of a drop on a folded card: reuse, existing jacks, saved project ----
// A cable dropped on a collapsed macro's card routes the way a cable dropped on a module inside it would
// (docs/macros/ports.md#how-a-port-is-drawn, docs/macros/auto-ports.md#ports-on-a-cable-drag): the same jack dropped
// again finds its port, a drop on an existing port jack leaves the source's own macro through that macro's ports and
// joins a port with nothing behind it to the module it was made for, and an input behaves like an output.

namespace {

// True when audio cables lead from `from` to `to`, through any number of nodes.
bool pathExists(AudioEngine& engine, NodeID from, NodeID to) {
    std::vector<NodeID> todo{from}, seen{from};
    while (!todo.empty()) {
        const auto at = todo.back();
        todo.pop_back();
        for (const auto& c : engine.getGraph().getConnections()) {
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

struct DropRig {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    juce::String targetId; // the folded macro: Filter -> Filter inside it
    juce::String outerId;  // the open macro the source sits in (empty when the source is bare)
    NodeID head;           // the first Filter inside the target
    NodeID tail;
    NodeID source;

    bool build(std::unique_ptr<juce::AudioProcessor> sourceProcessor, bool sourceInsideOpenMacro) {
        editor.setSize(2400, 1600);
        head = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 100);
        tail = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
        editor.connectPorts(head, 0, tail, 0, false, false);
        editor.setSelectedNodes({head, tail});
        targetId = editor.getMacroController().groupSelectionIntoMacro();
        source = addModuleAt(editor, engine, std::move(sourceProcessor), 900, 900);
        if (sourceInsideOpenMacro) {
            auto mate = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 1200);
            editor.setSelectedNodes({source, mate});
            outerId = editor.getMacroController().groupSelectionIntoMacro();
            editor.getMacroController().setMacroCollapsed(outerId, false);
            editor.updateComponents();
        }
        return !targetId.isEmpty() && compFor(editor, source) != nullptr;
    }

    const synth::Macro& target() { return *editor.getMacros().find(targetId); }
    size_t outerPorts() { return outerId.isEmpty() ? 0 : editor.getMacros().find(outerId)->ports.size(); }
    NodeID portNode(size_t index) { return nodeIdForUuid(engine, target().ports.at(index).nodeUuid); }
    MacroCardComponent& card() { return *editor.getMacroController().getMacroCardForTest(targetId); }
    juce::Point<int> cardCentre() { return card().localPointToGlobal(card().getLocalBounds().getCentre()); }

    // The card-local jack of the target's port `index` (`row` picks Left 0 or Right 1 of a two-jack port).
    juce::Point<int> jackOf(size_t index, int row = -1) {
        for (const auto& dot : editor.getMacroController().macroCardPortLayout(targetId))
            if (dot.nodeUuid == target().ports.at(index).nodeUuid && (row < 0 || dot.visibleJack == row))
                return card().localPointToGlobal(dot.jackPos);
        return {};
    }

    void dropAt(juce::Point<int> point, int jack, bool fromInput = false) {
        editor.beginConnectionDrag(compFor(editor, source), jack, fromInput, /*isMidi=*/false, {0, 0});
        editor.endConnectionDrag(point);
    }

    bool reaches(NodeID from, NodeID to) { return pathExists(engine, from, to); }
};

juce::StringArray portNames(const synth::Macro& macro) {
    juce::StringArray names;
    for (const auto& p : macro.ports)
        names.add(p.name);
    return names;
}

} // namespace

TEST(MacroPortFlowCardDrop, ALeftDropThenARightDropMakeALeftPortAndARightPortAndEachIsOneUndoStep) {
    DropRig rig;
    ASSERT_TRUE(rig.build(std::make_unique<ChannelStripModule>(), /*sourceInsideOpenMacro=*/true));

    rig.dropAt(rig.cardCentre(), 0);
    ASSERT_EQ(rig.target().ports.size(), 1u);
    EXPECT_EQ(rig.outerPorts(), 1u);

    rig.dropAt(rig.cardCentre(), 1);
    ASSERT_EQ(rig.target().ports.size(), 2u);
    EXPECT_EQ(rig.outerPorts(), 2u) << "the strip's macro leaves through one output per jack";
    EXPECT_EQ(portNames(rig.target()), juce::StringArray({"Left", "Right"}));
    EXPECT_TRUE(rig.reaches(rig.source, rig.portNode(0)));
    EXPECT_TRUE(rig.reaches(rig.source, rig.portNode(1)));

    // Dropping Left again on the card adds nothing and records nothing.
    rig.dropAt(rig.cardCentre(), 0);
    EXPECT_EQ(rig.target().ports.size(), 2u);
    EXPECT_EQ(rig.outerPorts(), 2u);

    ASSERT_TRUE(rig.undo.undo());
    EXPECT_EQ(rig.target().ports.size(), 1u) << "one Cmd+Z takes the Right drop away whole";
    EXPECT_EQ(rig.outerPorts(), 1u);
    ASSERT_TRUE(rig.undo.undo());
    EXPECT_EQ(rig.target().ports.size(), 0u);
    EXPECT_EQ(rig.outerPorts(), 0u);
}

TEST(MacroPortFlowCardDrop, TheSameOutputDroppedTwiceOnABareModuleKeepsOnePortAndOneCable) {
    DropRig rig;
    ASSERT_TRUE(rig.build(std::make_unique<ChannelStripModule>(), /*sourceInsideOpenMacro=*/false));
    rig.dropAt(rig.cardCentre(), 0);
    const auto cables = rig.engine.getGraph().getConnections().size();
    rig.dropAt(rig.cardCentre(), 0);
    EXPECT_EQ(rig.target().ports.size(), 1u);
    EXPECT_EQ(rig.engine.getGraph().getConnections().size(), cables);
}

// Every port shape a drop can make, and the two-jack Stereo shape a person makes in the dialog: the same source dropped
// twice leaves one port and the cables of one drop, in both directions. Mono, StereoCollapsed and Poly come from the
// source's own jack; Stereo is added first and the drop lands on its Left row.
TEST(MacroPortFlowCardDrop, EveryPortShapeAndDirectionDroppedTwiceMakesOnePort) {
    struct Case {
        const char* name;
        std::function<std::unique_ptr<juce::AudioProcessor>(bool fromInput)> make;
        std::optional<MacroPortShape> preMade; // set: the macro already has a port of this shape
        bool midi;
    };
    const std::vector<Case> cases = {
        {"Mono", [](bool) { return std::make_unique<OscillatorModule>(); }, std::nullopt, false},
        {"StereoCollapsed", [](bool) { return std::make_unique<TestStereoCollapsedModule>(); }, std::nullopt, false},
        {"Poly", [](bool) { return std::make_unique<TestPolyCVModule>(); }, std::nullopt, false},
        {"Stereo", [](bool) { return std::make_unique<TestStereoCollapsedModule>(); }, MacroPortShape::Stereo, false},
        {"Midi",
         [](bool fromInput) -> std::unique_ptr<juce::AudioProcessor> {
             if (fromInput)
                 return std::make_unique<OscillatorModule>(); // takes MIDI in
             return std::make_unique<MidiKeyboardModule>();   // sends MIDI out
         },
         std::nullopt, true},
    };
    for (const auto& c : cases)
        for (const bool fromInput : {false, true}) {
            SCOPED_TRACE(std::string(c.name) + (fromInput ? " dragged from an input" : " dragged from an output"));
            DropRig rig;
            ASSERT_TRUE(rig.build(c.make(fromInput), false));
            if (c.preMade)
                rig.editor.getMacroController().addMacroPort(rig.targetId, !fromInput, synth::MacroPortKind::AudioCV,
                                                             *c.preMade, 1, "Pre");
            const auto drop = [&] {
                rig.editor.beginConnectionDrag(compFor(rig.editor, rig.source), 0, fromInput, c.midi, {0, 0});
                rig.editor.endConnectionDrag(c.preMade ? rig.jackOf(0, 0) : rig.cardCentre());
            };
            drop();
            ASSERT_EQ(rig.target().ports.size(), 1u);
            const auto cables = rig.engine.getGraph().getConnections().size();
            const auto nodes = rig.engine.getGraph().getNumNodes();
            drop();
            EXPECT_EQ(rig.target().ports.size(), 1u);
            EXPECT_EQ(rig.engine.getGraph().getNumNodes(), nodes);
            EXPECT_EQ(rig.engine.getGraph().getConnections().size(), cables);
        }
}

TEST(MacroPortFlowCardDrop, ADropOnAnExistingEmptyInputJoinsItToTheFirstModuleInsideAndUndoTakesBothAway) {
    DropRig rig;
    ASSERT_TRUE(rig.build(std::make_unique<ChannelStripModule>(), false));
    rig.dropAt(rig.cardCentre(), 0); // makes the port; nothing is behind it
    const auto port = rig.portNode(0);
    EXPECT_FALSE(hasConnection(rig.engine, port, 0, rig.head, 0));

    rig.dropAt(rig.jackOf(0), 1); // the strip's Right onto the port's jack
    EXPECT_TRUE(hasConnection(rig.engine, port, 0, rig.head, 0)) << "the port is joined to the chain's first module";
    EXPECT_TRUE(rig.reaches(rig.source, rig.tail)) << "the sound passes through the macro";
    EXPECT_EQ(rig.target().ports.size(), 1u);

    ASSERT_TRUE(rig.undo.undo());
    EXPECT_FALSE(hasConnection(rig.engine, port, 0, rig.head, 0));
    EXPECT_EQ(rig.target().ports.size(), 1u) << "the first drop's port is still there";
}

TEST(MacroPortFlowCardDrop, APortWithAnInnerCableKeepsItAndTheDropFeedsIt) {
    DropRig rig;
    ASSERT_TRUE(rig.build(std::make_unique<ChannelStripModule>(), false));
    rig.dropAt(rig.cardCentre(), 0);
    const auto port = rig.portNode(0);
    rig.editor.connectPorts(port, 0, rig.tail, 0, false, false); // the person chose the second Filter
    rig.dropAt(rig.jackOf(0), 1);
    EXPECT_TRUE(hasConnection(rig.engine, port, 0, rig.tail, 0));
    EXPECT_FALSE(hasConnection(rig.engine, port, 0, rig.head, 0)) << "an existing inner cable is not second-guessed";
}

TEST(MacroPortFlowCardDrop, ADropFromInsideAnOpenMacroOnAnExistingInputLeavesThroughThatMacrosOwnOutput) {
    DropRig rig;
    ASSERT_TRUE(rig.build(std::make_unique<ChannelStripModule>(), true));
    rig.dropAt(rig.cardCentre(), 0);
    ASSERT_EQ(rig.outerPorts(), 1u);
    rig.dropAt(rig.jackOf(0), 1);
    EXPECT_EQ(rig.outerPorts(), 2u) << "the Right cable crosses the strip's macro border through its own output";
    EXPECT_FALSE(hasConnection(rig.engine, rig.source, 4, rig.portNode(0), 0)) << "no plain cable across the border";
    EXPECT_TRUE(rig.reaches(rig.source, rig.tail));
}

TEST(MacroPortFlowCardDrop, AStereoSourceOnATwoJackStereoPortWiresBothLegsAndBothInsideLegs) {
    DropRig rig;
    ASSERT_TRUE(rig.build(std::make_unique<TestStereoCollapsedModule>(), false));
    rig.editor.getMacroController().addMacroPort(rig.targetId, true, synth::MacroPortKind::AudioCV,
                                                 MacroPortShape::Stereo, 1, "In");
    const auto port = rig.portNode(0);
    rig.dropAt(rig.jackOf(0, 0), 0);

    constexpr int kRight = MacroInletModule::kRightBase;
    EXPECT_TRUE(hasConnection(rig.engine, rig.source, 0, port, 0));
    auto* filter = dynamic_cast<ModuleBase*>(rig.engine.getGraph().getNodeForId(rig.head)->getProcessor());
    ASSERT_NE(filter, nullptr);
    EXPECT_TRUE(hasConnection(rig.engine, port, 0, rig.head, 0));
    EXPECT_TRUE(hasConnection(rig.engine, port, kRight, rig.head, filter->rightAudioLegChannel()))
        << "the Right leg goes on to the Filter's Right";
}

// sibling-gap guard: a cable onto a module inside a macro, onto the macro's existing port jack and onto the
// card all take the same route out of the source's own macro: one output of it, one input of the target.
TEST(MacroPortFlowCardDrop, EveryDropTargetKindLeavesTheSourcesMacroByOneOutputIntoOneInput) {
    enum class Target { InnerJack, ExistingPortJack, CardBody };
    for (const auto kind : {Target::InnerJack, Target::ExistingPortJack, Target::CardBody}) {
        SCOPED_TRACE(kind == Target::InnerJack          ? "inner module jack"
                     : kind == Target::ExistingPortJack ? "existing port jack"
                                                        : "card body");
        DropRig rig;
        ASSERT_TRUE(rig.build(std::make_unique<ChannelStripModule>(), true));
        if (kind == Target::InnerJack) {
            rig.editor.getMacroController().setMacroCollapsed(rig.targetId, false);
            rig.editor.updateComponents();
            auto* filterCard = compFor(rig.editor, rig.head);
            ASSERT_NE(filterCard, nullptr);
            rig.dropAt(filterCard->getBounds().getPosition() + filterCard->getPortCenter(0, /*isInput=*/true), 0);
        } else if (kind == Target::ExistingPortJack) {
            rig.editor.getMacroController().addMacroPort(rig.targetId, true, synth::MacroPortKind::AudioCV,
                                                         MacroPortShape::Mono, 1, "In");
            rig.editor.connectPorts(rig.portNode(0), 0, rig.head, 0, false, false);
            rig.dropAt(rig.jackOf(0), 0);
        } else {
            rig.dropAt(rig.cardCentre(), 0);
        }
        EXPECT_EQ(rig.outerPorts(), 1u) << "one output on the strip's macro";
        ASSERT_EQ(rig.target().ports.size(), 1u) << "one input on the target";
        EXPECT_TRUE(rig.target().ports[0].isInput);
        EXPECT_TRUE(rig.reaches(rig.source, rig.portNode(0)));
        if (kind != Target::CardBody)
            EXPECT_TRUE(rig.reaches(rig.source, rig.head)) << "the signal reaches the module inside";
        const auto* outer = rig.editor.getMacros().find(rig.outerId);
        EXPECT_FALSE(outer->ports[0].isInput);
        EXPECT_FALSE(hasConnection(rig.engine, rig.source, 0, rig.portNode(0), 0))
            << "no plain cable across the border";
    }
}

// ---- Input and output across a border, on a saved project -------------------------------------------------------

namespace {

// Tools/ui-verify/fixtures/macro-input-drop.agsproj: a copy of a project a person saved. "Oscillator 1" is an open
// channel macro that holds a folded child "Macro" (Filter -> Delay, one input "Filter Audio", one output "Delay
// Audio"); "Reverb 1" and "Reverb 2" are bare modules, outside every macro.
struct SavedRig {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    synth::TimelineDoc timeline;
    synth::MidiRemoteProjectDoc midiRemote;
    juce::String parentId, childId;

    bool open() {
        const auto bundle = juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("macro-input-drop.agsproj");
        const auto result =
            synth::ProjectBundle::load(bundle, engine.getGraph(), timeline, editor.getPatchDocument(),
                                       editor.getMacros(), midiRemote, nullptr, nullptr, nullptr, nullptr);
        if (!result.ok)
            return false;
        editor.setSize(3600, 1600);
        editor.updateComponents();
        for (const auto& m : editor.getMacros().getAll()) {
            if (m.name == "Oscillator 1")
                parentId = m.id;
            if (m.name == "Macro")
                childId = m.id;
        }
        return !parentId.isEmpty() && !childId.isEmpty();
    }

    // The Reverb that sits outside Oscillator 1 (the project's second Reverb).
    NodeID outsideReverb() { return nodeIdForUuid(engine, "81693a72-6bd0-4e91-aa5b-c69cd7a8ebf3"); }

    size_t parentPorts(bool isInput) {
        size_t n = 0;
        for (const auto& p : editor.getMacros().find(parentId)->ports)
            n += p.isInput == isInput ? 1 : 0;
        return n;
    }

    // One drag from the outside Reverb's jack 0, released on the middle of the child card (no jack under it).
    bool dropOnChildBody(bool fromInput) {
        auto* card = editor.getMacroController().getMacroCardForTest(childId);
        auto* source = compFor(editor, outsideReverb());
        if (card == nullptr || !card->isVisible() || source == nullptr)
            return false;
        editor.beginConnectionDrag(source, 0, fromInput, false, {0, 0});
        editor.endConnectionDrag(card->localPointToGlobal(card->getLocalBounds().getCentre()));
        return true;
    }

    // One drag from jack 0 of `module` onto the child card's port jack on the side `portIsInput` names.
    bool dropOnChildPort(bool portIsInput) {
        auto* card = editor.getMacroController().getMacroCardForTest(childId);
        auto* source = compFor(editor, outsideReverb());
        if (card == nullptr || !card->isVisible() || source == nullptr)
            return false;
        for (const auto& dot : editor.getMacroController().macroCardPortLayout(childId))
            if (dot.isInput == portIsInput) {
                editor.beginConnectionDrag(source, 0, /*isInput=*/!portIsInput, false, {0, 0});
                editor.endConnectionDrag(card->localPointToGlobal(dot.jackPos));
                return true;
            }
        return false;
    }
};

} // namespace

// The project as saved: a bare module cabled to the folded child's input crosses Oscillator 1's border through a new
// input of Oscillator 1 that runs on to the Filter inside, exactly as a cable from the child's output goes out through
// a new output (input and output share one test so they stay symmetric).
TEST(MacroPortFlowCardDrop, ASavedProjectsMacroInputTakesACableAcrossABorderThroughAPortLikeAnOutputDoes) {
    {
        SavedRig rig;
        ASSERT_TRUE(rig.open());
        const auto reverb = rig.outsideReverb();
        ASSERT_NE(reverb.uid, 0u);
        const auto inputs = rig.parentPorts(true);
        ASSERT_TRUE(rig.dropOnChildPort(/*portIsInput=*/true));
        EXPECT_EQ(rig.parentPorts(true), inputs + 1) << "Oscillator 1 gains an input for the cable";
        for (const auto& c : rig.engine.getGraph().getConnections())
            if (c.source.nodeID == reverb && !c.source.isMIDI())
                EXPECT_TRUE(rig.editor.getMacros().findByMember(synth::AIStateMapper::ensureNodeUuid(
                                rig.engine.getGraph().getNodeForId(c.destination.nodeID))) != nullptr)
                    << "the cable ends on a port inside Oscillator 1, not straight on the child's port";
    }
    {
        SavedRig rig;
        ASSERT_TRUE(rig.open());
        const auto outputs = rig.parentPorts(false);
        ASSERT_TRUE(rig.dropOnChildPort(/*portIsInput=*/false));
        EXPECT_EQ(rig.parentPorts(false), outputs + 1) << "Oscillator 1 gains an output for the cable";
    }
    for (const bool fromInput : {false, true}) {
        SavedRig rig;
        ASSERT_TRUE(rig.open());
        const auto before = rig.parentPorts(!fromInput);
        ASSERT_TRUE(rig.dropOnChildBody(fromInput));
        EXPECT_EQ(rig.parentPorts(!fromInput), before + 1)
            << "a cable dropped on the card's body also crosses Oscillator 1's border through a port of its own";
    }
}
