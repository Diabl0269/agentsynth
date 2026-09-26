#include "AudioEngine/AudioEngine.h"
#include "MacroPortFlowTestHelpers.h"

// Topic: editing an existing port without changing its identity — remove, rename, reorder
// (adjacent-swap and drag-to-index), and the T152 per-port colour.

TEST(MacroPortFlow, RemoveDeletesTheNodeAndDropsThePort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(uuid.isEmpty());

    editor.getMacroController().removeMacroPort(macroId, uuid);

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
    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "Old Name");
    const int order = editor.getMacros().find(macroId)->ports[0].order;

    editor.getMacroController().renameMacroPort(macroId, uuid, "New Name");

    auto* port = &editor.getMacros().find(macroId)->ports[0];
    EXPECT_EQ(port->name, "New Name");
    EXPECT_EQ(port->order, order);
}

TEST(MacroPortFlow, RenameToBlankIsANoOp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "Keep Me");

    editor.getMacroController().renameMacroPort(macroId, uuid, "   ");

    EXPECT_EQ(editor.getMacros().find(macroId)->ports[0].name, "Keep Me");
}

TEST(MacroPortFlow, ReorderIsScopedToOneDirectionAndNoOpsAtTheEdge) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    const auto in1 = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                              MacroPortShape::Mono, 1, "In1");
    const auto in2 = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                              MacroPortShape::Mono, 1, "In2");
    const auto out1 = editor.getMacroController().addMacroPort(macroId, false, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "Out1");

    auto orderOf = [&](const juce::String& uuid) {
        for (const auto& p : editor.getMacros().find(macroId)->ports)
            if (p.nodeUuid == uuid)
                return p.order;
        return -1;
    };
    ASSERT_LT(orderOf(in1), orderOf(in2)); // In1 was added first

    editor.getMacroController().moveMacroPortOrder(macroId, in2, /*moveUp=*/true);
    EXPECT_LT(orderOf(in2), orderOf(in1)) << "In2 swapped ahead of In1";

    // Already first in its group: a further move-up is a no-op, not a crash or an order collision.
    editor.getMacroController().moveMacroPortOrder(macroId, in2, /*moveUp=*/true);
    EXPECT_LT(orderOf(in2), orderOf(in1));

    // The lone output never moves relative to the inputs — reorder is per-direction.
    const int out1OrderBefore = orderOf(out1);
    editor.getMacroController().moveMacroPortOrder(macroId, out1,
                                                   /*moveUp=*/false); // only output -> no-op (at the edge)
    EXPECT_EQ(orderOf(out1), out1OrderBefore);
}

// T152: drag-to-reorder's backing API. Unlike moveMacroPortOrder's adjacent swap, this can move a
// port an arbitrary number of slots in one call and renumbers the whole group sequentially.
TEST(MacroPortFlow, ReorderToIndexMovesAnArbitraryDistanceAndRenumbersTheWholeGroup) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    const auto in1 = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                              MacroPortShape::Mono, 1, "In1");
    const auto in2 = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                              MacroPortShape::Mono, 1, "In2");
    const auto in3 = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                              MacroPortShape::Mono, 1, "In3");

    auto orderOf = [&](const juce::String& uuid) {
        for (const auto& p : editor.getMacros().find(macroId)->ports)
            if (p.nodeUuid == uuid)
                return p.order;
        return -1;
    };
    ASSERT_LT(orderOf(in1), orderOf(in2));
    ASSERT_LT(orderOf(in2), orderOf(in3));

    // Drag In1 (currently index 0) to index 2 — the last slot — in one call.
    editor.getMacroController().reorderMacroPortToIndex(macroId, in1, 2);

    EXPECT_LT(orderOf(in2), orderOf(in3));
    EXPECT_LT(orderOf(in3), orderOf(in1)) << "In1 must now be LAST in its group";
}

TEST(MacroPortFlow, ReorderToIndexNeverTouchesTheOppositeDirectionsOrder) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);

    const auto in1 = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                              MacroPortShape::Mono, 1, "In1");
    const auto in2 = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                              MacroPortShape::Mono, 1, "In2");
    const auto out1 = editor.getMacroController().addMacroPort(macroId, false, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "Out1");
    const auto out2 = editor.getMacroController().addMacroPort(macroId, false, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "Out2");

    auto orderOf = [&](const juce::String& uuid) {
        for (const auto& p : editor.getMacros().find(macroId)->ports)
            if (p.nodeUuid == uuid)
                return p.order;
        return -1;
    };
    const int out1OrderBefore = orderOf(out1);
    const int out2OrderBefore = orderOf(out2);

    // Dragging an INPUT past whatever index an output group happens to share must never touch the
    // outputs — this is what makes "can't drag an input into the output section" structural: there
    // is no isInput parameter on reorderMacroPortToIndex at all for a cross-direction move to
    // express. An out-of-range index also just clamps to the group's own last slot.
    editor.getMacroController().reorderMacroPortToIndex(macroId, in1, 99);

    EXPECT_EQ(orderOf(out1), out1OrderBefore);
    EXPECT_EQ(orderOf(out2), out2OrderBefore);
    EXPECT_LT(orderOf(in2), orderOf(in1)) << "In1 clamped to the last slot in the INPUT group only";
}

TEST(MacroPortFlow, ReorderToIndexIsANoOpWhenAlreadyAtThatIndex) {
    AppUndoManager undo;
    AudioEngine engine;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const auto in1 = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                              MacroPortShape::Mono, 1, "In1");

    const int serialBefore = undo.getEditSerial();
    editor.getMacroController().reorderMacroPortToIndex(macroId, in1, 0); // already at index 0 — the only input
    EXPECT_EQ(undo.getEditSerial(), serialBefore) << "no-op must not push an undo entry";
}

// ============================================================================
// Per-port colour (T152)
// ============================================================================

TEST(MacroPortFlow, ChangeColourSetsAndClearsThePortsColour) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    const auto in1 = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                              MacroPortShape::Mono, 1, "Pitch In");

    auto colourOf = [&]() -> std::optional<juce::Colour> {
        for (const auto& p : editor.getMacros().find(macroId)->ports)
            if (p.nodeUuid == in1)
                return p.colour;
        return std::nullopt;
    };
    ASSERT_FALSE(colourOf().has_value());

    editor.changeMacroPortColour(macroId, in1, juce::Colour(0xff4fc1ff));
    ASSERT_TRUE(colourOf().has_value());
    EXPECT_EQ(*colourOf(), juce::Colour(0xff4fc1ff));

    editor.changeMacroPortColour(macroId, in1, std::nullopt);
    EXPECT_FALSE(colourOf().has_value());
}
