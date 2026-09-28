#include "AudioEngine/AudioEngine.h"
#include "MacroPortFlowTestHelpers.h"

// Topic: editing an existing port without changing its identity — remove, rename, reorder
// (adjacent-swap and drag-to-index), and the per-port colour.

namespace {
// Builds a two-member macro, an EXISTING port with a cable crossing through it (ext -> port -> a),
// and returns {macroId, portUuid, extNodeId, aNodeId} — the fixture the manual-delete tests
// share.
struct ManualDeletePortFixture {
    juce::String macroId;
    juce::String portUuid;
    NodeID ext;
    NodeID a;
};

ManualDeletePortFixture makeManualDeletePortFixture(GraphEditor& editor, AudioEngine& engine) {
    ManualDeletePortFixture fx;
    fx.macroId = makeTwoMemberMacro(editor, engine);
    auto* macro = editor.getMacros().find(fx.macroId);
    fx.a = nodeIdForUuid(engine, macro->members[0]);

    fx.portUuid = editor.getMacroController().addMacroPort(fx.macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                                           MacroPortShape::Mono, 1, "In");
    const auto portId = nodeIdForUuid(engine, fx.portUuid);

    fx.ext = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 50, 50);
    engine.getGraph().addConnection({{fx.ext, 0}, {portId, 0}});
    engine.getGraph().addConnection({{portId, 0}, {fx.a, 0}});
    return fx;
}
} // namespace

// ============================================================================
// The "splice the cable back" preference — Configure I/O's Delete Port (removeMacroPort)
// and the port's own right-click Delete Port both go through deleteMacroPortManually, so a flip
// of the preference always applies to both at once.
// ============================================================================

TEST(MacroPortFlow, ManualDeleteDropsTheCableByDefault) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto fx = makeManualDeletePortFixture(editor, engine);
    ASSERT_FALSE(editor.getSpliceCableOnMacroPortDeleteEnabled()) << "off by default";

    editor.getMacroController().deleteMacroPortManually(fx.macroId, fx.portUuid);

    EXPECT_TRUE(nodeIdForUuid(engine, fx.portUuid).uid == 0) << "the port node is gone";
    EXPECT_FALSE(hasConnection(engine, fx.ext, 0, fx.a, 0)) << "default: the cable is DROPPED, not spliced";
}

TEST(MacroPortFlow, ManualDeleteSplicesWhenThePreferenceIsOn) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto fx = makeManualDeletePortFixture(editor, engine);
    editor.setSpliceCableOnMacroPortDeleteEnabled(true);

    editor.getMacroController().deleteMacroPortManually(fx.macroId, fx.portUuid);

    EXPECT_TRUE(nodeIdForUuid(engine, fx.portUuid).uid == 0) << "the port node is gone";
    EXPECT_TRUE(hasConnection(engine, fx.ext, 0, fx.a, 0)) << "preference on: the cable is spliced back together";
}

// The right-click "Delete Port" menu item calls the exact same deleteMacroPortManually, so it must
// agree with Configure I/O's own Delete Port on both settings of the preference.
TEST(MacroPortFlow, RightClickDeletePortDropsByDefault) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto fx = makeManualDeletePortFixture(editor, engine);
    editor.getMacroController().setMacroCollapsed(fx.macroId, false); // ports get a ModuleComponent only while expanded

    const auto portId = nodeIdForUuid(engine, fx.portUuid);
    auto* portComp = compFor(editor, portId);
    ASSERT_NE(portComp, nullptr);

    const auto menu = portComp->buildMacroPortContextMenu();
    juce::PopupMenu::MenuItemIterator it(menu);
    bool ran = false;
    while (it.next()) {
        if (it.getItem().text == "Delete Port") {
            it.getItem().action();
            ran = true;
        }
    }
    ASSERT_TRUE(ran) << "the menu must actually offer Delete Port for an existing port";

    EXPECT_TRUE(nodeIdForUuid(engine, fx.portUuid).uid == 0);
    EXPECT_FALSE(hasConnection(engine, fx.ext, 0, fx.a, 0)) << "default: dropped, matching Configure I/O's own path";
}

TEST(MacroPortFlow, RightClickDeletePortSplicesWhenThePreferenceIsOn) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto fx = makeManualDeletePortFixture(editor, engine);
    editor.getMacroController().setMacroCollapsed(fx.macroId, false);
    editor.setSpliceCableOnMacroPortDeleteEnabled(true);

    const auto portId = nodeIdForUuid(engine, fx.portUuid);
    auto* portComp = compFor(editor, portId);
    ASSERT_NE(portComp, nullptr);

    const auto menu = portComp->buildMacroPortContextMenu();
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        if (it.getItem().text == "Delete Port")
            it.getItem().action();

    EXPECT_TRUE(nodeIdForUuid(engine, fx.portUuid).uid == 0);
    EXPECT_TRUE(hasConnection(engine, fx.ext, 0, fx.a, 0))
        << "preference on: spliced, matching Configure I/O's own path";
}

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

// drag-to-reorder's backing API. Unlike moveMacroPortOrder's adjacent swap, this can move a
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
// Per-port colour
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
