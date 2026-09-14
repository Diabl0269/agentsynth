// MacroAutoPortDeleteTests.cpp
// T148 (docs/macros_implementation.md §7 item 9): auto-delete a macro port once its last cable is gone, hooked at
// disconnectCable/disconnectPort. T154 extends the same auto-delete primitive to whole-node
// deletion (deleteSelection/deleteModule/requestDeleteModule) via
// GraphEditor::macroPortDeletionNeighbors. Shared test modules/helpers live in
// MacroAutoPortTestHelpers.h.
//
// Creation tests live in MacroAutoPortCreationTests.cpp; ungroup/presentation/modal-preference
// tests live in MacroAutoPortUngroupTests.cpp.

#include "MacroAutoPortTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/ReverbModule.h"
#include "Modules/FilterModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroMidiInletModule.h"
#include "Modules/MacroMidiOutletModule.h"
#include "Modules/MacroOutletModule.h"
#include "UI/Macros/MacroCardComponent.h"
#include "UI/Settings/PreferencesSettingsTab.h"

// ============================================================================
// T148 (docs/macros_implementation.md §7 item 9): auto-delete a macro port once its last cable is gone — the
// reverse of the auto-create-on-group behaviour above. GraphEditor::disconnectCable and
// disconnectPort are the two explicit user-gesture call sites hooked. T154 extends the same
// auto-delete primitive (autoDeleteOrphanedMacroPort) to whole-node deletion —
// deleteSelection/deleteModule/requestDeleteModule — via GraphEditor::macroPortDeletionNeighbors,
// which captures every node OUTSIDE a batch deletion with a live connection INTO it before the
// batch removal runs, then sweeps those candidates afterwards. requestDeleteModule is used below
// both as ordinary setup (stripping a macro down to just its port, as before) and, further down, as
// one of the hooked call sites in its own right.
// ============================================================================

namespace {
ModuleComponent* compForNode(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}
} // namespace

TEST(MacroAutoPortDelete, LastCableRemovedAutoDeletesThePortAndDissolvesTheMacroIfItWasTheLastMember) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro(); // no crossing -> zero ports
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    auto portId = nodeIdForUuid(engine, portUuid);
    ASSERT_TRUE(portId.uid != 0);

    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {portId, 0}}); // the port's ONE cable

    // Strip the macro down to just its port (ordinary setup — deleteModule is not a hooked site).
    editor.requestDeleteModule(a);
    editor.requestDeleteModule(b);
    portId = nodeIdForUuid(engine, portUuid); // re-resolve defensively; no undo/redo happened yet
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);
    ASSERT_TRUE(editor.getMacros().find(macroId)->memberIsPort(portUuid));

    auto* portComp = compForNode(editor, portId);
    ASSERT_NE(portComp, nullptr);
    editor.disconnectPort(portComp, 0, /*isInput=*/true, /*isMidi=*/false);

    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr) << "the port node is removed";
    EXPECT_TRUE(editor.getMacros().empty()) << "the port was the macro's last member";
}

TEST(MacroAutoPortDelete, DisconnectingOneOfTwoLegsLeavesThePortAlone) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    const auto portId = nodeIdForUuid(engine, portUuid);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {portId, 0}}); // exterior leg
    engine.getGraph().addConnection({{portId, 0}, {a, 0}});   // interior leg

    auto* portComp = compForNode(editor, portId);
    ASSERT_NE(portComp, nullptr);
    // Disconnect only the EXTERIOR leg (the port's own INPUT jack) — the interior leg must survive.
    editor.disconnectPort(portComp, 0, /*isInput=*/true, /*isMidi=*/false);

    EXPECT_NE(engine.getGraph().getNodeForId(portId), nullptr) << "one leg still wired -> the port survives";
    EXPECT_TRUE(hasConnection(engine, portId, 0, a, 0)) << "the interior leg is untouched";
    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
    EXPECT_TRUE(editor.getMacros().find(macroId)->memberIsPort(portUuid));
}

TEST(MacroAutoPortDelete, AFanInPortIsOnlyDeletedOnceEveryConnectionIsGone) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    const auto portId = nodeIdForUuid(engine, portUuid);
    auto ext1 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext1", 100, 100);
    auto ext2 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext2", 100, 300);
    engine.getGraph().addConnection({{ext1, 0}, {portId, 0}}); // fan-in: two external sources...
    engine.getGraph().addConnection({{ext2, 0}, {portId, 0}}); // ...sharing the same inlet port

    auto findCableInto = [&](NodeID src) {
        for (const auto& cable : editor.buildVisibleCables())
            if (cable.id.srcUid == src.uid && cable.id.dstUid == portId.uid)
                return cable;
        ADD_FAILURE() << "expected cable not found";
        return GraphEditor::VisibleCable{};
    };

    editor.disconnectCable(findCableInto(ext1));
    EXPECT_NE(engine.getGraph().getNodeForId(portId), nullptr) << "one of two fan-in cables gone: port survives";
    EXPECT_TRUE(hasConnection(engine, ext2, 0, portId, 0));
    ASSERT_NE(editor.getMacros().find(macroId), nullptr);

    editor.disconnectCable(findCableInto(ext2));
    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr) << "the last fan-in cable is now gone too";
}

TEST(MacroAutoPortDelete, AutoDeleteViaDisconnectPortIsOneUndoStepAndUndoRestoresPortMembershipAndBothCables) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    auto portId = nodeIdForUuid(engine, portUuid);
    auto ext1 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext1", 100, 100);
    auto ext2 = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext2", 100, 300);
    engine.getGraph().addConnection({{ext1, 0}, {portId, 0}}); // fan-in: BOTH land on the port's one
    engine.getGraph().addConnection({{ext2, 0}, {portId, 0}}); // input jack -> one disconnectPort call

    editor.requestDeleteModule(a);
    editor.requestDeleteModule(b);
    portId = nodeIdForUuid(engine, portUuid); // re-resolve defensively; no undo/redo happened yet
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);

    auto* portComp = compForNode(editor, portId);
    ASSERT_NE(portComp, nullptr);
    // Removes BOTH fan-in cables in one call — disconnectPort clears every connection on the jack.
    editor.disconnectPort(portComp, 0, /*isInput=*/true, /*isMidi=*/false);

    ASSERT_EQ(engine.getGraph().getNodeForId(portId), nullptr) << "zero connections left -> auto-deleted";
    ASSERT_TRUE(editor.getMacros().empty()) << "and it was the macro's last member";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    const auto restoredPortId = nodeIdForUuid(engine, portUuid);
    EXPECT_TRUE(restoredPortId.uid != 0) << "one Cmd+Z restores the port node";
    ASSERT_EQ(editor.getMacros().size(), 1u) << "and the macro record it dissolved";
    EXPECT_TRUE(editor.getMacros().getAll()[0].memberIsPort(portUuid));
    EXPECT_TRUE(hasConnection(engine, ext1, 0, restoredPortId, 0)) << "both original fan-in cables restored";
    EXPECT_TRUE(hasConnection(engine, ext2, 0, restoredPortId, 0));
}

TEST(MacroAutoPortDelete, AutoDeleteViaDisconnectCableIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    auto portId = nodeIdForUuid(engine, portUuid);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {portId, 0}}); // the port's ONE cable

    editor.requestDeleteModule(a);
    editor.requestDeleteModule(b);
    portId = nodeIdForUuid(engine, portUuid);
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);

    GraphEditor::VisibleCable cable;
    bool found = false;
    for (const auto& c : editor.buildVisibleCables())
        if (c.id.srcUid == ext.uid && c.id.dstUid == portId.uid) {
            cable = c;
            found = true;
        }
    ASSERT_TRUE(found);

    editor.disconnectCable(cable);
    ASSERT_EQ(engine.getGraph().getNodeForId(portId), nullptr);
    ASSERT_TRUE(editor.getMacros().empty());

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    const auto restoredPortId = nodeIdForUuid(engine, portUuid);
    EXPECT_TRUE(restoredPortId.uid != 0) << "one Cmd+Z restores the port node";
    ASSERT_EQ(editor.getMacros().size(), 1u);
    EXPECT_TRUE(hasConnection(engine, ext, 0, restoredPortId, 0));
}

TEST(MacroAutoPortDelete, DisabledPreferenceLeavesACablelessPortInPlaceRegressionGuard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    editor.setAutoDeleteMacroPortsOnLastCableEnabled(false);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    auto portId = nodeIdForUuid(engine, portUuid);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {portId, 0}}); // the port's ONE cable

    editor.requestDeleteModule(a);
    editor.requestDeleteModule(b);
    portId = nodeIdForUuid(engine, portUuid);
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);

    auto* portComp = compForNode(editor, portId);
    ASSERT_NE(portComp, nullptr);
    editor.disconnectPort(portComp, 0, /*isInput=*/true, /*isMidi=*/false);

    EXPECT_NE(engine.getGraph().getNodeForId(portId), nullptr)
        << "the toggle off means a cable-less port survives instead of being auto-deleted";
    ASSERT_FALSE(editor.getMacros().empty());
    EXPECT_TRUE(editor.getMacros().find(macroId)->memberIsPort(portUuid));
}

// ============================================================================
// T154 (docs/macros_implementation.md §7 item 9's follow-up): the same auto-delete primitive, now also swept
// after a whole-node deletion (deleteSelection/deleteModule/requestDeleteModule) via
// GraphEditor::macroPortDeletionNeighbors.
// ============================================================================

TEST(MacroAutoPortDelete, DeletingAnOrdinaryMemberViaRequestDeleteModuleStrandsAndSweepsThePortWhileTheMacroSurvives) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, portUuid);
    ASSERT_TRUE(portId.uid != 0);
    // The port's only connection is its interior leg into `a` — no exterior cable at all, so
    // deleting `a` alone drops the port to zero connections.
    engine.getGraph().addConnection({{portId, 0}, {a, 0}});
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 3u); // a, b, port

    editor.requestDeleteModule(a);

    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr) << "stranded port is spliced out";
    ASSERT_NE(editor.getMacros().find(macroId), nullptr) << "b is still a member -> macro survives";
    EXPECT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);
    EXPECT_FALSE(editor.getMacros().find(macroId)->memberIsPort(portUuid));
}

TEST(MacroAutoPortDelete, ABatchDeletionDoesNotTreatAConnectionBetweenTwoDeletedNodesAsAStrandingCandidate) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, portUuid);
    ASSERT_TRUE(portId.uid != 0);
    // Two connections: port -> a (crosses INTO the batch, a real candidate) and a -> b (BOTH
    // endpoints are inside the batch, so it must not surface `b` as a stranding candidate at all —
    // the exclusion branch macroPortDeletionNeighbors() itself relies on).
    engine.getGraph().addConnection({{portId, 0}, {a, 0}});
    engine.getGraph().addConnection({{a, 0}, {b, 0}});

    editor.setSelectedNodes({a, b});
    editor.deleteSelection();

    EXPECT_EQ(engine.getGraph().getNodeForId(a), nullptr);
    EXPECT_EQ(engine.getGraph().getNodeForId(b), nullptr);
    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr) << "the port -> a leg still strands the port";
    EXPECT_TRUE(editor.getMacros().empty()) << "both real members and the port are gone";
}

TEST(MacroAutoPortDelete, DeletingTheLastOrdinaryMemberViaDeleteSelectionStrandsThePortAndDissolvesTheMacroToo) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    // Strip to just `a` (ordinary setup, mirrors the T148 tests above).
    editor.requestDeleteModule(b);
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, portUuid);
    ASSERT_TRUE(portId.uid != 0);
    engine.getGraph().addConnection({{portId, 0}, {a, 0}});          // the port's ONLY connection
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 2u); // a, port

    editor.setSelectedNodes({a});
    editor.deleteSelection();

    EXPECT_EQ(engine.getGraph().getNodeForId(a), nullptr);
    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr) << "stranded port is spliced out";
    EXPECT_TRUE(editor.getMacros().empty()) << "the port was the macro's last remaining member";
}

TEST(MacroAutoPortDelete, FanOutPortWithASurvivingConnectionIsNotSweptJustBecauseOneNeighborWasDeletedInTheSameBatch) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.requestDeleteModule(b); // strip to just `a`, ordinary setup
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/false, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Out");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, portUuid);
    ASSERT_TRUE(portId.uid != 0);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 700, 100);
    engine.getGraph().addConnection({{a, 0}, {portId, 0}});   // interior leg -> `a`, about to be deleted
    engine.getGraph().addConnection({{portId, 0}, {ext, 0}}); // exterior leg -> `ext`, survives

    editor.requestDeleteModule(a);

    EXPECT_NE(engine.getGraph().getNodeForId(portId), nullptr)
        << "the exterior leg still lives -> the port is not swept";
    EXPECT_TRUE(hasConnection(engine, portId, 0, ext, 0));
    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
    EXPECT_TRUE(editor.getMacros().find(macroId)->memberIsPort(portUuid));
}

TEST(MacroAutoPortDelete, AutoDeleteViaDeleteSelectionIsOneUndoStepAndUndoRestoresTheMemberAndThePort) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    const auto aUuid = engine.getGraph().getNodeForId(a)->properties["uuid"].toString();
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.requestDeleteModule(b); // strip to just `a`, ordinary setup
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, portUuid);
    engine.getGraph().addConnection({{portId, 0}, {a, 0}}); // the port's ONLY connection

    editor.setSelectedNodes({a});
    editor.deleteSelection();

    ASSERT_EQ(engine.getGraph().getNodeForId(a), nullptr);
    ASSERT_EQ(engine.getGraph().getNodeForId(portId), nullptr);
    ASSERT_TRUE(editor.getMacros().empty());

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    const auto restoredA = nodeIdForUuid(engine, aUuid);
    EXPECT_TRUE(restoredA.uid != 0) << "one Cmd+Z restores the deleted member";
    const auto restoredPortId = nodeIdForUuid(engine, portUuid);
    EXPECT_TRUE(restoredPortId.uid != 0) << "and the auto-deleted port too";
    ASSERT_EQ(editor.getMacros().size(), 1u) << "and the macro record it dissolved";
    EXPECT_TRUE(editor.getMacros().getAll()[0].memberIsPort(portUuid));
    EXPECT_TRUE(hasConnection(engine, restoredPortId, 0, restoredA, 0));
}

TEST(MacroAutoPortDelete, DisabledPreferenceLeavesACablelessPortInPlaceAfterDeleteSelectionRegressionGuard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    editor.setAutoDeleteMacroPortsOnLastCableEnabled(false);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.requestDeleteModule(b); // strip to just `a`, ordinary setup
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);

    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, portUuid);
    engine.getGraph().addConnection({{portId, 0}, {a, 0}}); // the port's ONLY connection

    editor.setSelectedNodes({a});
    editor.deleteSelection();

    EXPECT_EQ(engine.getGraph().getNodeForId(a), nullptr);
    EXPECT_NE(engine.getGraph().getNodeForId(portId), nullptr)
        << "the toggle off means a cable-less port survives instead of being auto-deleted";
    ASSERT_FALSE(editor.getMacros().empty());
    EXPECT_TRUE(editor.getMacros().find(macroId)->memberIsPort(portUuid));
}
