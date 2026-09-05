// GraphEditor-level tests for the Macro I/O port lifecycle (P8-15b, T140): the "Configure I/O"
// modal's underlying API (docs/macros.md §7 items 3+5, unified per the founder's explicit request)
// plus the "shape from a dropped cable" convenience (§5.3). MacroPortConfigDialogTests.cpp covers
// the dialog itself in isolation; this file covers what its callbacks are meant to reach, and the
// cable-drop path the dialog is not involved in at all.
//
//   • add          — Mono/Stereo/Poly-N/MIDI ports, each a new node + a synth::MacroPort entry
//   • remove       — deletes the node, drops the port via the existing retainOnly() mechanism
//   • rename       — touches only the port's name
//   • reorder      — move-up/move-down scoped to one direction, no-op at either edge
//   • shape change — THE load-bearing case (docs/macros.md §5.3): delete-node + create-node +
//                    rewire lands as ONE undo step, external AND internal cables on a
//                    still-active raw channel survive, cables on a channel the new shape drops are
//                    dropped (not silently adapted)
//   • cable drop   — dragging a cable onto a collapsed macro card creates a Mono port and wires
//                    the external end, with no dialog involved
//   • library/size — the four node types stay out of the module library, with a size estimate
//                    pinned against the real rendered card (docs/macros.md §7 item 3's own
//                    "estimateModuleSize needs an entry" requirement)

#include "../Source/AI/AIStateMapper.h"
#include "../Source/AppUndoManager.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/MacroInletModule.h"
#include "../Source/Modules/MacroMidiOutletModule.h"
#include "../Source/Modules/MacroOutletModule.h"
#include "../Source/Modules/MidiKeyboardModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/WavetableOscillatorModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/MacroCardComponent.h"
#include "../Source/UI/ModuleComponent.h"
#include "../Source/UI/ModuleLibraryComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

juce::String uuidOf(AudioEngine& engine, NodeID id) {
    auto* node = engine.getGraph().getNodeForId(id);
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

NodeID nodeIdForUuid(AudioEngine& engine, const juce::String& uuid) {
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

bool hasConnection(AudioEngine& engine, NodeID srcId, int srcCh, NodeID dstId, int dstCh) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == srcId && c.source.channelIndex == srcCh && c.destination.nodeID == dstId &&
            c.destination.channelIndex == dstCh)
            return true;
    return false;
}

/** Groups two fresh Oscillator/Filter modules into a new collapsed macro (the min-2 rule) and
 *  returns its id. */
juce::String makeTwoMemberMacro(GraphEditor& editor, AudioEngine& engine) {
    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    return editor.groupSelectionIntoMacro();
}

/** The live ModuleComponent for `id`, or nullptr — the same lookup every drag-based test below
 *  needs to resolve a NodeID to something endConnectionDrag's own jack hit-test loop can see. */
ModuleComponent* compFor(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

} // namespace

// ============================================================================
// Add
// ============================================================================

TEST(MacroPortFlow, AddMonoInputCreatesAMacroInletMemberAndPort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());

    const auto uuid = editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                          MacroPortShape::Mono, 1, "Pitch In");
    ASSERT_FALSE(uuid.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->hasMember(uuid));
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_EQ(macro->ports[0].nodeUuid, uuid);
    EXPECT_TRUE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].name, "Pitch In");
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::AudioCV);

    auto nodeId = nodeIdForUuid(engine, uuid);
    auto* inlet = dynamic_cast<MacroInletModule*>(engine.getGraph().getNodeForId(nodeId)->getProcessor());
    ASSERT_NE(inlet, nullptr);
    EXPECT_EQ(inlet->getPortShape(), MacroPortShape::Mono);
}

TEST(MacroPortFlow, AddOutputMidiCreatesAMacroMidiOutletMember) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    const auto uuid =
        editor.addMacroPort(macroId, /*isInput=*/false, synth::MacroPortKind::Midi, MacroPortShape::Mono, 1, "");
    ASSERT_FALSE(uuid.isEmpty());

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_FALSE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::Midi);
    EXPECT_EQ(macro->ports[0].name, "MIDI Out"); // blank name falls back to the direction/kind default

    auto nodeId = nodeIdForUuid(engine, uuid);
    auto* proc = engine.getGraph().getNodeForId(nodeId)->getProcessor();
    ASSERT_NE(dynamic_cast<MacroMidiOutletModule*>(proc), nullptr);
    // Pass-through, like MacroMidiInletModule: both true (an internal member feeds it MIDI in,
    // it hands MIDI out to the world) — the outlet/inlet distinction is which SIDE of the macro
    // boundary the node sits on, not an asymmetry in acceptsMidi()/producesMidi().
    EXPECT_TRUE(proc->producesMidi());
    EXPECT_TRUE(proc->acceptsMidi());
}

TEST(MacroPortFlow, AddStereoAndPolySetTheChosenShapeOnTheNode) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    const auto stereoUuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Stereo, 1, "Stereo In");
    auto* stereoInlet = dynamic_cast<MacroInletModule*>(
        engine.getGraph().getNodeForId(nodeIdForUuid(engine, stereoUuid))->getProcessor());
    ASSERT_NE(stereoInlet, nullptr);
    EXPECT_EQ(stereoInlet->getPortShape(), MacroPortShape::Stereo);
    EXPECT_EQ(stereoInlet->getVisibleInputPortCount(), 2);

    const auto polyUuid =
        editor.addMacroPort(macroId, false, synth::MacroPortKind::AudioCV, MacroPortShape::Poly, 5, "Poly Out");
    auto* polyOutlet = dynamic_cast<MacroOutletModule*>(
        engine.getGraph().getNodeForId(nodeIdForUuid(engine, polyUuid))->getProcessor());
    ASSERT_NE(polyOutlet, nullptr);
    EXPECT_EQ(polyOutlet->getPortShape(), MacroPortShape::Poly);
    EXPECT_EQ(polyOutlet->getVoiceCount(), 5);
    EXPECT_EQ(polyOutlet->getVisibleOutputPortCount(), 1); // one fanned jack, not five
}

TEST(MacroPortFlow, AddIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const int nodesBefore = engine.getGraph().getNodes().size();

    const auto uuid = editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(uuid.isEmpty());
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore);
    EXPECT_TRUE(nodeIdForUuid(engine, uuid).uid == 0);
    EXPECT_TRUE(editor.getMacros().find(macroId)->ports.empty());
}

// ============================================================================
// Remove
// ============================================================================

TEST(MacroPortFlow, RemoveDeletesTheNodeAndDropsThePort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const auto uuid = editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(uuid.isEmpty());

    editor.removeMacroPort(macroId, uuid);

    EXPECT_TRUE(nodeIdForUuid(engine, uuid).uid == 0);
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr); // the two original members keep the macro alive
    EXPECT_FALSE(macro->hasMember(uuid));
    EXPECT_TRUE(macro->ports.empty());
}

// ============================================================================
// Rename / reorder
// ============================================================================

TEST(MacroPortFlow, RenameTouchesOnlyTheName) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const auto uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Old Name");
    const int order = editor.getMacros().find(macroId)->ports[0].order;

    editor.renameMacroPort(macroId, uuid, "New Name");

    auto* port = &editor.getMacros().find(macroId)->ports[0];
    EXPECT_EQ(port->name, "New Name");
    EXPECT_EQ(port->order, order);
}

TEST(MacroPortFlow, RenameToBlankIsANoOp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const auto uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Keep Me");

    editor.renameMacroPort(macroId, uuid, "   ");

    EXPECT_EQ(editor.getMacros().find(macroId)->ports[0].name, "Keep Me");
}

TEST(MacroPortFlow, ReorderIsScopedToOneDirectionAndNoOpsAtTheEdge) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    const auto in1 = editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In1");
    const auto in2 = editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In2");
    const auto out1 =
        editor.addMacroPort(macroId, false, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Out1");

    auto orderOf = [&](const juce::String& uuid) {
        for (const auto& p : editor.getMacros().find(macroId)->ports)
            if (p.nodeUuid == uuid)
                return p.order;
        return -1;
    };
    ASSERT_LT(orderOf(in1), orderOf(in2)); // In1 was added first

    editor.moveMacroPortOrder(macroId, in2, /*moveUp=*/true);
    EXPECT_LT(orderOf(in2), orderOf(in1)) << "In2 swapped ahead of In1";

    // Already first in its group: a further move-up is a no-op, not a crash or an order collision.
    editor.moveMacroPortOrder(macroId, in2, /*moveUp=*/true);
    EXPECT_LT(orderOf(in2), orderOf(in1));

    // The lone output never moves relative to the inputs — reorder is per-direction.
    const int out1OrderBefore = orderOf(out1);
    editor.moveMacroPortOrder(macroId, out1, /*moveUp=*/false); // only output -> no-op (at the edge)
    EXPECT_EQ(orderOf(out1), out1OrderBefore);
}

// ============================================================================
// Shape change — the load-bearing case
// ============================================================================

TEST(MacroPortFlow, ChangeShapeIsOneUndoStepAndPreservesCablesOnStillActiveChannels) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    const auto memberIds = macro->members; // Oscillator, Filter uuids
    const auto filterId = nodeIdForUuid(engine, memberIds[1]);

    const auto oldUuid = editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                             MacroPortShape::Mono, 1, "Pitch In");
    ASSERT_FALSE(oldUuid.isEmpty());
    const auto oldNodeId = nodeIdForUuid(engine, oldUuid);

    // An external node (not a macro member) feeding the port's input, and the port's output
    // feeding an internal member — both on raw channel 0, which stays active under every shape.
    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    engine.getGraph().addConnection({{extOsc, 0}, {oldNodeId, 0}});
    engine.getGraph().addConnection({{oldNodeId, 0}, {filterId, 0}});
    ASSERT_TRUE(hasConnection(engine, extOsc, 0, oldNodeId, 0));
    ASSERT_TRUE(hasConnection(engine, oldNodeId, 0, filterId, 0));

    const int nodesBefore = engine.getGraph().getNodes().size();

    const auto newUuid = editor.changeMacroPortShape(macroId, oldUuid, MacroPortShape::Stereo, 1);
    ASSERT_FALSE(newUuid.isEmpty());
    EXPECT_NE(newUuid, oldUuid);
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore); // delete + create: net zero nodes
    EXPECT_TRUE(nodeIdForUuid(engine, oldUuid).uid == 0) << "old node is gone";

    auto* macroAfter = editor.getMacros().find(macroId);
    ASSERT_EQ(macroAfter->ports.size(), 1u);
    EXPECT_EQ(macroAfter->ports[0].nodeUuid, newUuid);
    EXPECT_EQ(macroAfter->ports[0].name, "Pitch In"); // identity preserved across the shape change
    EXPECT_TRUE(macroAfter->ports[0].isInput);
    EXPECT_TRUE(macroAfter->hasMember(newUuid));
    EXPECT_FALSE(macroAfter->hasMember(oldUuid));

    const auto newNodeId = nodeIdForUuid(engine, newUuid);
    EXPECT_TRUE(hasConnection(engine, extOsc, 0, newNodeId, 0)) << "external cable replayed on ch0";
    EXPECT_TRUE(hasConnection(engine, newNodeId, 0, filterId, 0)) << "internal cable replayed on ch0";

    // ---- The load-bearing assertion: ONE undo restores BOTH the graph and the macro together. ----
    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_TRUE(nodeIdForUuid(engine, newUuid).uid == 0) << "the shape-changed node is gone after undo";
    const auto restoredNodeId = nodeIdForUuid(engine, oldUuid);
    EXPECT_FALSE(restoredNodeId.uid == 0) << "the original node uuid is back";

    auto* macroRestored = editor.getMacros().find(macroId);
    ASSERT_EQ(macroRestored->ports.size(), 1u);
    EXPECT_EQ(macroRestored->ports[0].nodeUuid, oldUuid);
    EXPECT_EQ(macroRestored->ports[0].name, "Pitch In");

    const auto restoredFilterId = nodeIdForUuid(engine, memberIds[1]);
    const auto restoredExtOsc = extOsc; // graph I/O node ids for pre-existing nodes are unaffected by undo here
    EXPECT_TRUE(hasConnection(engine, restoredExtOsc, 0, restoredNodeId, 0)) << "external cable restored";
    EXPECT_TRUE(hasConnection(engine, restoredNodeId, 0, restoredFilterId, 0)) << "internal cable restored";
}

TEST(MacroPortFlow, ChangeShapeDropsACableOnARawChannelTheNewShapeNoLongerExposes) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    const auto uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Stereo, 1, "Stereo In");
    const auto nodeId = nodeIdForUuid(engine, uuid);

    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    // Wired onto the Stereo shape's RIGHT leg (kRightBase), which Mono does not expose.
    engine.getGraph().addConnection({{extOsc, 0}, {nodeId, MacroInletModule::kRightBase}});
    ASSERT_TRUE(hasConnection(engine, extOsc, 0, nodeId, MacroInletModule::kRightBase));

    const auto newUuid = editor.changeMacroPortShape(macroId, uuid, MacroPortShape::Mono, 1);
    ASSERT_FALSE(newUuid.isEmpty());
    const auto newNodeId = nodeIdForUuid(engine, newUuid);

    for (const auto& c : engine.getGraph().getConnections())
        EXPECT_FALSE(c.destination.nodeID == newNodeId && c.destination.channelIndex == MacroInletModule::kRightBase)
            << "a raw channel Mono no longer exposes must not carry a replayed cable";
}

TEST(MacroPortFlow, ChangeShapeIsANoOpForAMidiPort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const auto uuid = editor.addMacroPort(macroId, true, synth::MacroPortKind::Midi, MacroPortShape::Mono, 1, "");

    const auto result = editor.changeMacroPortShape(macroId, uuid, MacroPortShape::Stereo, 1);
    EXPECT_TRUE(result.isEmpty());
    EXPECT_FALSE(nodeIdForUuid(engine, uuid).uid == 0) << "the original MIDI node is untouched";
}

// ============================================================================
// Cable-drop convenience (§5.3)
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

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);
    ASSERT_TRUE(card->isVisible());
    const auto dropPoint = card->getBounds().getCentre();

    // Drag from the external Oscillator's audio OUTPUT (jack 0) and release over the collapsed
    // card — no jack under the cursor (T141 hasn't drawn any yet), so this exercises the
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

    auto* card = editor.getMacroCardForTest(macroId);
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

// ============================================================================
// Library absence + pinned size estimate (docs/macros.md §7 item 3's own requirement)
// ============================================================================

TEST(MacroPortFlow, AllFourTypesAreAbsentFromTheLibraryWithAPinnedSizeEstimate) {
    ModuleLibraryComponent library;
    for (const juce::String& typeName : {"Macro In", "Macro Out", "Macro MIDI In", "Macro MIDI Out"}) {
        EXPECT_FALSE(library.getDraggableModuleNames().contains(typeName))
            << typeName << " is internal-only and must stay out of the module library";

        auto processor = synth::AIStateMapper::createModule(typeName);
        ASSERT_NE(processor, nullptr) << typeName;

        AudioEngine engine;
        GraphEditor editor(engine);
        ModuleComponent comp(processor.get(), NodeID(1), editor);

        const auto estimate = GraphEditor::estimateModuleSize(typeName);
        EXPECT_EQ(estimate.x, comp.getWidth()) << typeName;
        EXPECT_EQ(estimate.y, comp.getHeight()) << typeName;
    }
}

// ============================================================================
// T148 (docs/macros.md §7 item 9): auto-create a macro port when a dragged cable crosses an
// EXPANDED macro's boundary — the counterpart to the collapsed-card drop convenience above, which
// only fires when there is no jack under the cursor. All jack-to-jack, so both endpoints are real,
// visible ModuleComponents this time (no MacroCardComponent involved).
// ============================================================================

TEST(MacroPortFlow, DraggingFromAnExpandedMacroMemberToAnExternalModuleAutoCreatesAnOutletAndWiresBothLegs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto oscMember = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filterMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({oscMember, filterMember});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false); // expand: members become real, visible ModuleComponents

    auto* memberComp = compFor(editor, oscMember);
    ASSERT_NE(memberComp, nullptr);
    ASSERT_TRUE(memberComp->isVisible());

    auto extFilter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    auto* extComp = compFor(editor, extFilter);
    ASSERT_NE(extComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    // Oscillator's audio OUTPUT (jack 0, the member) -> the external Filter's audio INPUT (jack 0).
    editor.beginConnectionDrag(memberComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(extComp->getBounds().getPosition() + extComp->getPortCenter(0, /*isInput=*/true));

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1) << "exactly one new outlet port node";

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_FALSE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::AudioCV);

    const auto portId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    ASSERT_TRUE(portId.uid != 0);
    EXPECT_TRUE(hasConnection(engine, oscMember, 0, portId, 0)) << "interior leg: member -> port";
    EXPECT_TRUE(hasConnection(engine, portId, 0, extFilter, 0)) << "exterior leg: port -> external";
    EXPECT_FALSE(hasConnection(engine, oscMember, 0, extFilter, 0)) << "the direct cable must not also exist";
}

TEST(MacroPortFlow, DraggingFromAnExternalModuleToAnExpandedMacroMemberAutoCreatesAnInletAndWiresBothLegs) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto oscMember = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filterMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({oscMember, filterMember});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    auto* memberComp = compFor(editor, filterMember);
    ASSERT_NE(memberComp, nullptr);

    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 100);
    auto* extComp = compFor(editor, extOsc);
    ASSERT_NE(extComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    // External Oscillator's OUTPUT (jack 0) -> the member Filter's audio INPUT (jack 0).
    editor.beginConnectionDrag(extComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(memberComp->getBounds().getPosition() + memberComp->getPortCenter(0, /*isInput=*/true));

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1) << "exactly one new inlet port node";

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u);
    EXPECT_TRUE(macro->ports[0].isInput);

    const auto portId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    ASSERT_TRUE(portId.uid != 0);
    EXPECT_TRUE(hasConnection(engine, extOsc, 0, portId, 0)) << "exterior leg: external -> port";
    EXPECT_TRUE(hasConnection(engine, portId, 0, filterMember, 0)) << "interior leg: port -> member";
}

TEST(MacroPortFlow, DraggingBetweenMembersOfTwoDifferentMacrosCreatesAPortOnEachWithAPortToPortConnection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    // Macro X: an Oscillator (the drag source) plus a filler member (min-2 rule).
    auto oscX = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto fillerX = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({oscX, fillerX});
    const auto macroXId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroXId.isEmpty());
    editor.setMacroCollapsed(macroXId, false);

    // Macro Y: a Filter (the drag destination) plus a filler member.
    auto filterY = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    auto fillerY = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 400);
    editor.setSelectedNodes({filterY, fillerY});
    const auto macroYId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroYId.isEmpty());
    editor.setMacroCollapsed(macroYId, false);

    auto* oscXComp = compFor(editor, oscX);
    auto* filterYComp = compFor(editor, filterY);
    ASSERT_NE(oscXComp, nullptr);
    ASSERT_NE(filterYComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(oscXComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(filterYComp->getBounds().getPosition() + filterYComp->getPortCenter(0, /*isInput=*/true));

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 2) << "one outlet on X, one inlet on Y";

    auto* macroX = editor.getMacros().find(macroXId);
    auto* macroY = editor.getMacros().find(macroYId);
    ASSERT_NE(macroX, nullptr);
    ASSERT_NE(macroY, nullptr);
    ASSERT_EQ(macroX->ports.size(), 1u);
    ASSERT_EQ(macroY->ports.size(), 1u);
    EXPECT_FALSE(macroX->ports[0].isInput) << "X gets an outlet";
    EXPECT_TRUE(macroY->ports[0].isInput) << "Y gets an inlet";

    const auto outletId = nodeIdForUuid(engine, macroX->ports[0].nodeUuid);
    const auto inletId = nodeIdForUuid(engine, macroY->ports[0].nodeUuid);
    EXPECT_TRUE(hasConnection(engine, oscX, 0, outletId, 0)) << "X's interior leg";
    EXPECT_TRUE(hasConnection(engine, outletId, 0, inletId, 0)) << "the port-to-port boundary leg";
    EXPECT_TRUE(hasConnection(engine, inletId, 0, filterY, 0)) << "Y's interior leg";
}

TEST(MacroPortFlow, DraggingBetweenTwoMembersOfTheSameMacroCreatesAPlainDirectConnectionRegressionGuard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto oscMember = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filterMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({oscMember, filterMember});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    auto* oscComp = compFor(editor, oscMember);
    auto* filterComp = compFor(editor, filterMember);
    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(filterComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(oscComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(filterComp->getBounds().getPosition() + filterComp->getPortCenter(0, /*isInput=*/true));

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore) << "no port minted for a same-macro connection";
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->ports.empty());
    EXPECT_TRUE(hasConnection(engine, oscMember, 0, filterMember, 0)) << "plain direct connection instead";
}

TEST(MacroPortFlow, DraggingToAnExistingPortDirectlyDoesNotMintASecondPortRegressionGuard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto macroId = makeTwoMemberMacro(editor, engine);
    editor.setMacroCollapsed(macroId, false);

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, portUuid);
    ASSERT_TRUE(portId.uid != 0);
    auto* portComp = compFor(editor, portId);
    ASSERT_NE(portComp, nullptr);

    auto extOsc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 900);
    auto* extComp = compFor(editor, extOsc);
    ASSERT_NE(extComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    // Drag straight onto the existing inlet port's own jack (isInput on the port side).
    editor.beginConnectionDrag(extComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(portComp->getBounds().getPosition() + portComp->getPortCenter(0, /*isInput=*/true));

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore) << "no second port minted";
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u) << "still just the one hand-added port";
    EXPECT_TRUE(hasConnection(engine, extOsc, 0, portId, 0)) << "wired straight into the existing port";
}

TEST(MacroPortFlow,
     DraggingAModulationConnectionAcrossAMacroBoundaryAutoCreatesAPortAndWrapsTheModTargetLegInAnAttenuverter) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    // LFO is the macro member (the drag source); a Filter fills the min-2 rule.
    auto lfoMember = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 100, 100);
    auto fillerMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({lfoMember, fillerMember});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    auto* lfoComp = compFor(editor, lfoMember);
    ASSERT_NE(lfoComp, nullptr);

    // External Wavetable Oscillator: its "Position" slider is a real modulation target, the same
    // Serum-style knob-drop DroppingACableOnAKnobCreatesAModRouting (GraphEditorTests.cpp) exercises.
    auto extWt = addModuleAt(editor, engine, std::make_unique<WavetableOscillatorModule>(), 900, 100);
    auto* extComp = compFor(editor, extWt);
    ASSERT_NE(extComp, nullptr);

    juce::Slider* position = nullptr;
    for (auto* child : extComp->getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Position")
                position = s;
    ASSERT_NE(position, nullptr);

    const auto knobPoint = extComp->getBounds().getPosition() + position->getBounds().getCentre();
    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(lfoComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(knobPoint);

    // T155: a mod-routed drag now mints a port too, exactly like a plain audio drag — TWO new
    // nodes, not one: the auto-created MacroOutletModule AND the hidden AttenuverterModule that
    // connectPorts()'s own CV detection wraps the port->realDestination leg in.
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 2) << "one port node, one attenuverter node";

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u) << "the port, and only the port, is a macro member";
    EXPECT_FALSE(macro->ports[0].isInput);
    EXPECT_EQ(macro->ports[0].kind, synth::MacroPortKind::AudioCV);

    const auto portId = nodeIdForUuid(engine, macro->ports[0].nodeUuid);
    ASSERT_TRUE(portId.uid != 0);
    EXPECT_TRUE(hasConnection(engine, lfoMember, 0, portId, 0)) << "interior leg: member -> port, plain and direct";

    // The exterior leg (port -> the real modulation-target destination) is the one that must be
    // wrapped: NOT a direct connection, but a hidden attenuverter chain, reported by the mod
    // matrix as one active routing whose source is the port (not the LFO, and not the Attenuverter
    // node itself — mirrors MacroAutoPortTests.cpp's
    // AttenuverterAdjacentCrossingIsSplicedForAGenuineExternalCrossing assertion style).
    auto active = engine.getActiveModRoutings();
    ASSERT_EQ(active.size(), 1u) << "exactly one mod routing, wrapping the port->destination leg";
    EXPECT_EQ(active[0].sourceNodeID, portId) << "the routing's source is the port, not the LFO directly";
    EXPECT_EQ(active[0].destNodeID, extWt) << "the routing's destination is the real modulation target";
    const auto attenId = active[0].attenuverterNodeID;
    ASSERT_TRUE(attenId.uid != 0);
    EXPECT_TRUE(hasConnection(engine, portId, 0, attenId, 0)) << "port -> attenuverter";
    EXPECT_TRUE(hasConnection(engine, attenId, 0, extWt, active[0].destChannelIndex))
        << "attenuverter -> real destination";
    EXPECT_FALSE(hasConnection(engine, portId, 0, extWt, active[0].destChannelIndex))
        << "no leftover direct port->destination connection";

    // Neither the attenuverter nor the port's own uuid appears anywhere but the one macro entry —
    // the attenuverter can never itself be a macro member (docs/macros.md §2/§7 item 7).
    EXPECT_FALSE(macro->hasMember(uuidOf(engine, attenId)));
}

TEST(MacroPortFlow, TheWholeModCVAutoCreateSequenceIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto lfoMember = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 100, 100);
    auto fillerMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({lfoMember, fillerMember});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    auto* lfoComp = compFor(editor, lfoMember);
    ASSERT_NE(lfoComp, nullptr);
    auto extWt = addModuleAt(editor, engine, std::make_unique<WavetableOscillatorModule>(), 900, 100);
    auto* extComp = compFor(editor, extWt);
    ASSERT_NE(extComp, nullptr);

    juce::Slider* position = nullptr;
    for (auto* child : extComp->getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Position")
                position = s;
    ASSERT_NE(position, nullptr);
    const auto knobPoint = extComp->getBounds().getPosition() + position->getBounds().getCentre();

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(lfoComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(knobPoint);

    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 2) << "port node + attenuverter node";
    ASSERT_EQ(engine.getActiveModRoutings().size(), 1u);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore)
        << "a single Cmd+Z removed the port AND the attenuverter";
    EXPECT_TRUE(editor.getMacros().find(macroId)->ports.empty());
    EXPECT_TRUE(engine.getActiveModRoutings().empty()) << "the mod routing itself is undone too";

    ASSERT_TRUE(undo.canRedo());
    undo.redo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 2);
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
    const auto portId = nodeIdForUuid(engine, editor.getMacros().find(macroId)->ports[0].nodeUuid);
    ASSERT_TRUE(portId.uid != 0);
    EXPECT_TRUE(hasConnection(engine, lfoMember, 0, portId, 0));
    auto active = engine.getActiveModRoutings();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0].sourceNodeID, portId);
    EXPECT_EQ(active[0].destNodeID, extWt);
}

TEST(MacroPortFlow,
     DraggingAModCrossingBetweenMembersOfTwoDifferentMacrosCreatesAPortOnEachWithTheAttenuverterOnTheFinalLeg) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    // Macro X: the LFO (the drag source) plus a filler member (min-2 rule).
    auto lfoX = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 100, 100);
    auto fillerX = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({lfoX, fillerX});
    const auto macroXId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroXId.isEmpty());
    editor.setMacroCollapsed(macroXId, false);

    // Macro Y: a Wavetable Oscillator (its "Position" slider is the drag destination, a real
    // modulation target) plus a filler member.
    auto wtY = addModuleAt(editor, engine, std::make_unique<WavetableOscillatorModule>(), 900, 100);
    auto fillerY = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 400);
    editor.setSelectedNodes({wtY, fillerY});
    const auto macroYId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroYId.isEmpty());
    editor.setMacroCollapsed(macroYId, false);

    auto* lfoXComp = compFor(editor, lfoX);
    auto* wtYComp = compFor(editor, wtY);
    ASSERT_NE(lfoXComp, nullptr);
    ASSERT_NE(wtYComp, nullptr);

    juce::Slider* position = nullptr;
    for (auto* child : wtYComp->getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child))
            if (s->getComponentID() == "Position")
                position = s;
    ASSERT_NE(position, nullptr);
    const auto knobPoint = wtYComp->getBounds().getPosition() + position->getBounds().getCentre();

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(lfoXComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(knobPoint);

    // One outlet on X, one inlet on Y, and the hidden attenuverter — three new nodes total.
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 3) << "outlet + inlet + attenuverter";

    auto* macroX = editor.getMacros().find(macroXId);
    auto* macroY = editor.getMacros().find(macroYId);
    ASSERT_NE(macroX, nullptr);
    ASSERT_NE(macroY, nullptr);
    ASSERT_EQ(macroX->ports.size(), 1u);
    ASSERT_EQ(macroY->ports.size(), 1u);
    EXPECT_FALSE(macroX->ports[0].isInput) << "X gets an outlet";
    EXPECT_TRUE(macroY->ports[0].isInput) << "Y gets an inlet";

    const auto outletId = nodeIdForUuid(engine, macroX->ports[0].nodeUuid);
    const auto inletId = nodeIdForUuid(engine, macroY->ports[0].nodeUuid);
    ASSERT_TRUE(outletId.uid != 0);
    ASSERT_TRUE(inletId.uid != 0);

    // X's interior leg and the port-to-port boundary leg are both plain, direct connections — the
    // outlet is not itself a modulation target, so neither leg attracts the wrap.
    EXPECT_TRUE(hasConnection(engine, lfoX, 0, outletId, 0)) << "X's interior leg: plain";
    EXPECT_TRUE(hasConnection(engine, outletId, 0, inletId, 0)) << "the port-to-port boundary leg: plain";

    // Only Y's interior leg (inlet -> the real modulation-target destination) is wrapped — it's
    // the last leg wired, and the only one whose real endpoint is a genuine modulation target.
    auto active = engine.getActiveModRoutings();
    ASSERT_EQ(active.size(), 1u) << "exactly one mod routing, on Y's interior leg only";
    EXPECT_EQ(active[0].sourceNodeID, inletId);
    EXPECT_EQ(active[0].destNodeID, wtY);
    const auto attenId = active[0].attenuverterNodeID;
    ASSERT_TRUE(attenId.uid != 0);
    EXPECT_TRUE(hasConnection(engine, inletId, 0, attenId, 0)) << "inlet -> attenuverter";
    EXPECT_TRUE(hasConnection(engine, attenId, 0, wtY, active[0].destChannelIndex))
        << "attenuverter -> real destination";
    EXPECT_FALSE(hasConnection(engine, inletId, 0, wtY, active[0].destChannelIndex))
        << "no leftover direct inlet->destination connection";
}

TEST(MacroPortFlow, TheWholeAutoCreateSequenceIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto oscMember = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filterMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({oscMember, filterMember});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    auto* memberComp = compFor(editor, oscMember);
    ASSERT_NE(memberComp, nullptr);
    auto extFilter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    auto* extComp = compFor(editor, extFilter);
    ASSERT_NE(extComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(memberComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(extComp->getBounds().getPosition() + extComp->getPortCenter(0, /*isInput=*/true));

    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore) << "a single Cmd+Z removed the port node too";
    EXPECT_TRUE(editor.getMacros().find(macroId)->ports.empty());
    EXPECT_FALSE(hasConnection(engine, oscMember, 0, extFilter, 0)) << "no leftover cable from the mint-and-wire";

    ASSERT_TRUE(undo.canRedo());
    undo.redo();

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 1);
    ASSERT_EQ(editor.getMacros().find(macroId)->ports.size(), 1u);
    const auto portId = nodeIdForUuid(engine, editor.getMacros().find(macroId)->ports[0].nodeUuid);
    EXPECT_TRUE(hasConnection(engine, oscMember, 0, portId, 0));
    EXPECT_TRUE(hasConnection(engine, portId, 0, extFilter, 0));
}

TEST(MacroPortFlow, AutoCreateOnDragDisabledPreferenceLeavesTheOriginalBehaviourRegressionGuard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    editor.setAutoCreateMacroPortsOnDragEnabled(false);

    auto oscMember = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filterMember = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    editor.setSelectedNodes({oscMember, filterMember});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    auto* memberComp = compFor(editor, oscMember);
    ASSERT_NE(memberComp, nullptr);
    auto extFilter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    auto* extComp = compFor(editor, extFilter);
    ASSERT_NE(extComp, nullptr);

    const int nodesBefore = engine.getGraph().getNodes().size();

    editor.beginConnectionDrag(memberComp, 0, /*isInput=*/false, /*isMidi=*/false, {0, 0});
    editor.endConnectionDrag(extComp->getBounds().getPosition() + extComp->getPortCenter(0, /*isInput=*/true));

    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore) << "the toggle off means no port is minted";
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_TRUE(macro->ports.empty());
    EXPECT_TRUE(hasConnection(engine, oscMember, 0, extFilter, 0)) << "plain direct connection instead";
}
