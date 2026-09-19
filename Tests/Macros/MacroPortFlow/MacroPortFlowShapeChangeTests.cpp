#include "MacroPortFlowTestHelpers.h"

// Topic: shape change — the load-bearing case (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed):
// delete-node + create-node + rewire as ONE undo step, cable survival/drop by raw channel, MIDI no-op.

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
