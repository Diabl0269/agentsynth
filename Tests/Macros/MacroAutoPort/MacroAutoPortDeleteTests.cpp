// MacroAutoPortDeleteTests.cpp
// Auto-deleting a macro port once its last cable is gone (docs/macros/auto-ports.md#ports-on-a-cable-drag), hooked at
// disconnectCable/disconnectPort, and the same auto-delete primitive extended to whole-node deletion
// (deleteSelection/deleteModule/requestDeleteModule) via GraphEditor::macroPortDeletionNeighbors. Shared test
// modules/helpers live in MacroAutoPortTestHelpers.h.
//
// Creation tests live in MacroAutoPortCreationTests.cpp; ungroup/presentation/modal-preference
// tests live in MacroAutoPortUngroupTests.cpp.

#include "AudioEngine/AudioEngine.h"
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
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"

// ============================================================================
// Auto-delete a macro port once its last cable is gone — the
// reverse of the auto-create-on-group behaviour above. GraphEditor::disconnectCable and
// disconnectPort are the two explicit user-gesture call sites hooked. Whole-node deletion extends the same
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
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(); // no crossing -> zero ports
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
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

// Regression test for FRO564: an inlet left with nothing feeding it kept its inside leg.
TEST(MacroAutoPortDelete, DisconnectingTheOuterLegClearsThePortAndItsInsideLeg) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    const auto portId = nodeIdForUuid(engine, portUuid);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {portId, 0}}); // exterior leg
    engine.getGraph().addConnection({{portId, 0}, {a, 0}});   // interior leg

    auto* portComp = compForNode(editor, portId);
    ASSERT_NE(portComp, nullptr);
    // Disconnect the EXTERIOR leg (the port's own INPUT jack): nothing feeds the port any more.
    editor.disconnectPort(portComp, 0, /*isInput=*/true, /*isMidi=*/false);

    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr) << "an inlet nothing feeds is removed";
    EXPECT_FALSE(hasConnection(engine, portId, 0, a, 0)) << "its inside leg goes with it";
    ASSERT_NE(editor.getMacros().find(macroId), nullptr);
    EXPECT_FALSE(editor.getMacros().find(macroId)->memberIsPort(portUuid));
}

TEST(MacroAutoPortDelete, DisconnectingTheInnerLegLeavesThePortAlone) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    const auto portId = nodeIdForUuid(engine, portUuid);
    auto ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    engine.getGraph().addConnection({{ext, 0}, {portId, 0}}); // exterior leg
    engine.getGraph().addConnection({{portId, 0}, {a, 0}});   // interior leg

    for (const auto& cable : editor.buildVisibleCables())
        if (cable.id.srcUid == portId.uid && cable.id.dstUid == a.uid)
            editor.disconnectCable(cable);

    EXPECT_NE(engine.getGraph().getNodeForId(portId), nullptr) << "still fed from outside -> waiting to be patched";
    EXPECT_TRUE(hasConnection(engine, ext, 0, portId, 0)) << "the exterior leg is untouched";
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
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
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
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
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
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
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
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
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
// The same auto-delete primitive, also swept after a whole-node deletion
// (deleteSelection/deleteModule/requestDeleteModule) via GraphEditor::macroPortDeletionNeighbors
// (docs/macros/auto-ports.md#auto-deleting-a-port-when-its-last-cable-goes).
// ============================================================================

TEST(MacroAutoPortDelete, DeletingAnOrdinaryMemberViaRequestDeleteModuleStrandsAndSweepsThePortWhileTheMacroSurvives) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
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
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
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
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    // Strip to just `a` (ordinary setup, mirrors the auto-create tests above).
    editor.requestDeleteModule(b);
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
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
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.requestDeleteModule(b); // strip to just `a`, ordinary setup
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/false, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Out");
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
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.requestDeleteModule(b); // strip to just `a`, ordinary setup
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
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
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    editor.requestDeleteModule(b); // strip to just `a`, ordinary setup
    ASSERT_EQ(editor.getMacros().find(macroId)->members.size(), 1u);

    const auto portUuid = editor.getMacroController().addMacroPort(
        macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
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

// ============================================================================
// A bounded one-extra-hop-through-an-attenuverter
// special case. Almost every real macro-port-to-modulation-target crossing is spliced through a
// hidden AttenuverterModule (AudioEngine::addModRouting), which can never itself be a macro member
// -- so deleting the FAR side of that chain used to strand the port forever, two hops away from
// the deleted node.
// ============================================================================

// The default-patch shape named in the ticket: Env -> Attenuverter -> Filter cutoff, grouped, then
// the FAR module (Filter) deleted. The port fronting Env's mod-CV output routes through the
// attenuverter to reach Filter; deleting Filter leaves the attenuverter with exactly one
// connection left (back to the port), which must delete the attenuverter AND the port together.
TEST(MacroAutoPortDelete, DeletingTheFarModuleThroughAHiddenAttenuverterSweepsTheAttenuverterAndThePort) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto env = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Env", 100, 100);
    auto filter = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Filter", 900, 100);
    auto spare = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Spare", 100, 400);
    const auto attenId = engine.addModRouting(env, 0, filter, 0); // Env -> Attenuverter -> Filter cutoff
    ASSERT_TRUE(attenId.uid != 0);

    // Group Env with a second member (the min-2 rule) -- Env's own mod-routed crossing to the
    // attenuverter gets a real port; Filter stays outside.
    editor.setSelectedNodes({env, spare});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u) << "sanity: Env's mod-routed crossing got a real port";
    const auto portUuid = macro->ports.front().nodeUuid;
    const auto portId = nodeIdForUuid(engine, portUuid);
    ASSERT_TRUE(portId.uid != 0);
    ASSERT_NE(engine.getGraph().getNodeForId(attenId), nullptr) << "sanity: the attenuverter itself is untouched";

    editor.requestDeleteModule(filter);

    EXPECT_EQ(engine.getGraph().getNodeForId(filter), nullptr);
    EXPECT_EQ(engine.getGraph().getNodeForId(attenId), nullptr)
        << "the orphaned attenuverter (one remaining connection, to the port) must be swept too";
    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr)
        << "the port -- dead-ended now that its only exterior route (through the attenuverter) is gone -- must go too";
    macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr) << "Env and Spare are still members -- the macro survives";
    EXPECT_FALSE(macro->memberIsPort(portUuid));
    EXPECT_TRUE(macro->hasMember(engine.getGraph().getNodeForId(env)->properties["uuid"].toString()));
}

// Same shape as above, but Env's mod-CV output fans out through TWO attenuverters -- one to
// Filter's cutoff, one to a second module's (VCA's) gain -- both crossing the macro boundary
// through the SAME port. Deleting Filter must only sweep the Filter-side attenuverter; the port
// itself still has a live exterior connection (through the surviving attenuverter to VCA), so it
// must NOT be spliced away.
TEST(MacroAutoPortDelete, DeletingOneFanOutDestinationLeavesThePortWiredToTheOtherAttenuverter) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto env = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Env", 100, 100);
    auto filter = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Filter", 900, 100);
    auto vca = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "VCA", 900, 400);
    auto spare = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Spare", 100, 400);
    const auto attenToFilter = engine.addModRouting(env, 0, filter, 0);
    const auto attenToVca = engine.addModRouting(env, 0, vca, 0);
    ASSERT_TRUE(attenToFilter.uid != 0);
    ASSERT_TRUE(attenToVca.uid != 0);

    editor.setSelectedNodes({env, spare});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u) << "sanity: both fan-out legs share ONE crossing port";
    const auto portUuid = macro->ports.front().nodeUuid;
    const auto portId = nodeIdForUuid(engine, portUuid);
    ASSERT_TRUE(portId.uid != 0);

    editor.requestDeleteModule(filter);

    EXPECT_EQ(engine.getGraph().getNodeForId(filter), nullptr);
    EXPECT_EQ(engine.getGraph().getNodeForId(attenToFilter), nullptr)
        << "the Filter-side attenuverter is now orphaned and must be swept";
    EXPECT_NE(engine.getGraph().getNodeForId(attenToVca), nullptr) << "the VCA-side attenuverter is untouched";
    macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u) << "the port survives -- it still feeds the VCA leg";
    EXPECT_TRUE(macro->memberIsPort(portUuid));
    EXPECT_NE(engine.getGraph().getNodeForId(portId), nullptr);
    EXPECT_TRUE(hasConnection(engine, portId, 0, attenToVca, 0))
        << "the surviving fan-out leg must still be wired through its own attenuverter";
}

TEST(MacroAutoPortDelete, DeletingOneFanOutDestinationIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto env = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Env", 100, 100);
    auto filter = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Filter", 900, 100);
    auto vca = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "VCA", 900, 400);
    auto spare = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Spare", 100, 400);
    const auto attenToFilter = engine.addModRouting(env, 0, filter, 0);
    const auto attenToVca = engine.addModRouting(env, 0, vca, 0);
    ASSERT_TRUE(attenToFilter.uid != 0);
    ASSERT_TRUE(attenToVca.uid != 0);

    editor.setSelectedNodes({env, spare});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u);
    const auto portUuid = macro->ports.front().nodeUuid;
    const auto portId = nodeIdForUuid(engine, portUuid);
    undo.clearUndoHistory();

    editor.requestDeleteModule(filter);
    ASSERT_EQ(engine.getGraph().getNodeForId(attenToFilter), nullptr);
    ASSERT_NE(engine.getGraph().getNodeForId(portId), nullptr) << "sanity: the port survived the first delete";

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_NE(engine.getGraph().getNodeForId(filter), nullptr) << "one undo restores Filter";
    EXPECT_NE(engine.getGraph().getNodeForId(attenToFilter), nullptr) << "...and its attenuverter";
    macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u) << "the port never moved, so this is still just the one member";
    EXPECT_EQ(macro->ports.front().nodeUuid, portUuid);
    EXPECT_TRUE(hasConnection(engine, portId, 0, attenToFilter, 0));
    EXPECT_TRUE(hasConnection(engine, attenToFilter, 0, filter, 0));
    EXPECT_TRUE(hasConnection(engine, portId, 0, attenToVca, 0)) << "the VCA leg was never disturbed";
}

TEST(MacroAutoPortDelete, DeletingTheFarModuleThroughAnAttenuverterIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto env = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Env", 100, 100);
    auto filter = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Filter", 900, 100);
    auto spare = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Spare", 100, 400);
    const auto attenId = engine.addModRouting(env, 0, filter, 0);
    ASSERT_TRUE(attenId.uid != 0);

    editor.setSelectedNodes({env, spare});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(true);
    ASSERT_FALSE(macroId.isEmpty());
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u);
    const auto portUuid = macro->ports.front().nodeUuid;
    undo.clearUndoHistory();

    editor.requestDeleteModule(filter);
    ASSERT_EQ(nodeIdForUuid(engine, portUuid).uid, 0u) << "sanity: the port really was swept";
    ASSERT_EQ(engine.getGraph().getNodeForId(attenId), nullptr);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_NE(engine.getGraph().getNodeForId(filter), nullptr) << "one undo restores Filter";
    EXPECT_NE(engine.getGraph().getNodeForId(attenId), nullptr) << "...and the attenuverter";
    macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    ASSERT_EQ(macro->ports.size(), 1u) << "...and the macro port, together, in ONE undo step";
    EXPECT_EQ(macro->ports.front().nodeUuid, portUuid);
    const auto restoredPortId = nodeIdForUuid(engine, portUuid);
    EXPECT_TRUE(hasConnection(engine, restoredPortId, 0, attenId, 0));
    EXPECT_TRUE(hasConnection(engine, attenId, 0, filter, 0));
}

// ============================================================================
// sweepOneSidedMacroPorts (reached through applyProgrammaticConnectionChange): a macro dissolves only when
// it has no direct members AND no children, and dissolving cascades to a parent left empty.
// ============================================================================

namespace {
// Macro {a, b} with one input port wired from an external module; returns the port's uuid.
struct SweepSetup {
    NodeID a, b, ext;
    juce::String macroId, portUuid;
};

SweepSetup makeMacroWithWiredPort(AudioEngine& engine, GraphEditor& editor) {
    SweepSetup s;
    s.a = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "A", 400, 100);
    s.b = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "B", 400, 300);
    s.ext = addModuleAt(editor, engine, std::make_unique<TestMonoModule>(), "Ext", 100, 100);
    editor.setSelectedNodes({s.a, s.b});
    s.macroId = editor.getMacroController().groupSelectionIntoMacro();
    s.portUuid = editor.getMacroController().addMacroPort(s.macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV,
                                                          MacroPortShape::Mono, 1, "In");
    return s;
}

// The graph mutation a sweep test hands to applyProgrammaticConnectionChange: drop ext -> port.
bool dropExternalCable(AudioEngine& engine, const SweepSetup& s) {
    return engine.getGraph().removeConnection({{s.ext, 0}, {nodeIdForUuid(engine, s.portUuid), 0}});
}
} // namespace

TEST(MacroAutoPortDelete, SweepOfAOneSidedPortKeepsAFlatMacroThatStillHasMembers) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    const auto s = makeMacroWithWiredPort(engine, editor);
    ASSERT_FALSE(s.portUuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, s.portUuid);
    engine.getGraph().addConnection({{s.ext, 0}, {portId, 0}});

    editor.getMacroController().applyProgrammaticConnectionChange(false, [&] { return dropExternalCable(engine, s); });

    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr) << "the one-sided port is swept";
    const auto* macro = editor.getMacros().find(s.macroId);
    ASSERT_NE(macro, nullptr) << "the flat macro keeps its ordinary members";
    EXPECT_EQ(macro->members.size(), 2u);
}

TEST(MacroAutoPortDelete, SweepDissolvesAChildLeftEmptyAndThenItsParentLeftWithNothing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    const auto s = makeMacroWithWiredPort(engine, editor);
    ASSERT_FALSE(s.portUuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, s.portUuid);
    engine.getGraph().addConnection({{s.ext, 0}, {portId, 0}});

    // Leave the child with only its port as a direct member, inside a parent that has no members of its own.
    auto& macros = editor.getMacros();
    auto* child = macros.find(s.macroId);
    child->members = {s.portUuid};
    synth::Macro parent;
    parent.name = "Parent";
    const auto parentId = macros.add(parent);
    ASSERT_TRUE(macros.setParent(s.macroId, parentId));
    const auto nodesBefore = engine.getGraph().getNumNodes();

    editor.getMacroController().applyProgrammaticConnectionChange(false, [&] { return dropExternalCable(engine, s); });

    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr) << "the one-sided port is swept";
    EXPECT_EQ(macros.find(s.macroId), nullptr) << "the child had no members and no children left";
    EXPECT_EQ(macros.find(parentId), nullptr) << "and the parent, with no members and no children, goes too";
    EXPECT_TRUE(macros.empty());
    EXPECT_EQ(engine.getGraph().getNumNodes(), nodesBefore - 1) << "only the port node was removed";
    EXPECT_NE(engine.getGraph().getNodeForId(s.a), nullptr);
    EXPECT_NE(engine.getGraph().getNodeForId(s.ext), nullptr);
}
