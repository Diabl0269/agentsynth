// FRO168: a member of macro A Cmd-dragged out of A and into expanded macro B's hull transfers in
// ONE gesture and ONE undo step (leave A, join B); the "drag modules into and out of macros without
// Cmd" preference (default ON) lets a plain single-module drag do the same; and a library module
// dropped over an expanded hull joins it, in the same undo step as its creation.
//
// Like MacroDragMembershipTests.cpp, every gesture test drives the REAL ModuleComponent
// mouseDown/mouseDrag/mouseUp (and the real itemDragEnter/Move/Dropped for the library drop), never
// a direct addSelectionToMacro/removeSelectionFromMacro call. The one exception is called out where
// it appears: TransferOutOfASingleMemberMacro..., which reaches a state the geometry gate makes
// unreachable by mouse and calls finalizeMacroMembershipDrag itself.

#include "AppUndoManager.h"
#include "MacroDragTestHelpers.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <set>

namespace {

struct TwoMacros {
    NodeID a1, a2, b1, b2;
    juce::String macroA, macroB;
};

/** Two expanded 2-member macros with disjoint hulls: A on the left, B on the right. */
TwoMacros makeTwoExpandedMacros(GraphEditor& editor, AudioEngine& engine) {
    TwoMacros t;
    t.a1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    t.a2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 400);
    t.b1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 100);
    t.b2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 400);
    editor.setSelectedNodes({t.a1, t.a2});
    t.macroA = editor.getMacroController().groupSelectionIntoMacro();
    editor.setSelectedNodes({t.b1, t.b2});
    t.macroB = editor.getMacroController().groupSelectionIntoMacro();
    editor.getMacroController().setMacroCollapsed(t.macroA, false);
    editor.getMacroController().setMacroCollapsed(t.macroB, false);
    return t;
}

juce::Point<int> deltaIntoHull(GraphEditor& editor, ModuleComponent& comp, const juce::String& macroId) {
    return editor.getMacroController().macroHullBounds(macroId).getCentre() - comp.getBounds().getCentre();
}

bool isMemberOf(GraphEditor& editor, const juce::String& macroId, const juce::String& uuid) {
    const auto* macro = editor.getMacros().find(macroId);
    return macro != nullptr && macro->hasMember(uuid);
}

int countMacroPortNodes(AudioEngine& engine) {
    int n = 0;
    for (auto* node : engine.getGraph().getNodes()) {
        const auto name = node->getProcessor()->getName();
        if (name.contains("Macro In") || name.contains("Macro Out") || name.contains("MacroPort"))
            ++n;
    }
    return n;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// (a) Transfer A -> B in one Cmd gesture.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragTransfer, CmdDragMemberOfAIntoBHullLeavesAAndJoinsB) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1400);
    const auto t = makeTwoExpandedMacros(editor, engine);
    ASSERT_FALSE(t.macroA.isEmpty());
    ASSERT_FALSE(t.macroB.isEmpty());

    const auto uuidA1 = uuidOf(engine, t.a1);
    auto* comp = findComponent(editor, t.a1);
    ASSERT_NE(comp, nullptr);

    dragBodyBy(*comp, deltaIntoHull(editor, *comp, t.macroB), kCmdClick, [&] {
        EXPECT_EQ(editor.getMacroDragLeaveId(), t.macroA) << "mid-drag: A must be the LEAVE candidate";
        EXPECT_EQ(editor.getMacroDragJoinId(), t.macroB) << "mid-drag: B must be the JOIN candidate (A is skipped)";
    });

    EXPECT_FALSE(isMemberOf(editor, t.macroA, uuidA1)) << "the dragged module must have left A";
    EXPECT_TRUE(isMemberOf(editor, t.macroB, uuidA1)) << "and joined B in the same gesture";
    EXPECT_TRUE(isMemberOf(editor, t.macroA, uuidOf(engine, t.a2))) << "A keeps its other member";
    EXPECT_TRUE(isMemberOf(editor, t.macroB, uuidOf(engine, t.b1)));
    EXPECT_TRUE(isMemberOf(editor, t.macroB, uuidOf(engine, t.b2)));
    EXPECT_FALSE(editor.hasMacroDragCandidate()) << "no candidate left highlighted";
    EXPECT_FALSE(editor.getDragDropController().isDragPreviewActive());
}

// A member of a 3-member macro transferring out leaves a still-valid 2-member macro behind.
TEST(MacroDragTransfer, ThreeMemberSourceMacroStaysValidAfterATransfer) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1400);
    const auto t = makeTwoExpandedMacros(editor, engine);
    const auto a3 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 700);
    ASSERT_NE(editor.getMacroController().macroForNode(t.a1), nullptr);
    editor.getMacroController().addSelectionToMacro(t.macroA, {uuidOf(engine, a3)});
    ASSERT_TRUE(isMemberOf(editor, t.macroA, uuidOf(engine, a3)));

    const auto uuidA1 = uuidOf(engine, t.a1);
    auto* comp = findComponent(editor, t.a1);
    ASSERT_NE(comp, nullptr);
    dragBodyBy(*comp, deltaIntoHull(editor, *comp, t.macroB), kCmdClick);

    const auto* macroA = editor.getMacros().find(t.macroA);
    ASSERT_NE(macroA, nullptr) << "the source macro must survive";
    EXPECT_EQ(macroA->members.size(), 2u);
    EXPECT_TRUE(macroA->hasMember(uuidOf(engine, t.a2)));
    EXPECT_TRUE(macroA->hasMember(uuidOf(engine, a3)));
    EXPECT_FALSE(macroA->hasMember(uuidA1));
    EXPECT_TRUE(isMemberOf(editor, t.macroB, uuidA1));
}

// ---------------------------------------------------------------------------------------------
// (b) ONE undo restores A membership, B unchanged, and the pre-drag position; redo replays it.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragTransfer, OneUndoStepRestoresBothMacrosAndThePosition) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(2000, 1400);
    const auto t = makeTwoExpandedMacros(editor, engine);

    const auto uuidA1 = uuidOf(engine, t.a1);
    const auto uuidB1 = uuidOf(engine, t.b1);
    const auto uuidB2 = uuidOf(engine, t.b2);
    const int originalX = engine.getGraph().getNodeForId(t.a1)->properties["x"];
    const int originalY = engine.getGraph().getNodeForId(t.a1)->properties["y"];
    auto* comp = findComponent(editor, t.a1);
    ASSERT_NE(comp, nullptr);

    const int serialBefore = undo.getEditSerial();
    dragBodyBy(*comp, deltaIntoHull(editor, *comp, t.macroB), kCmdClick);
    ASSERT_TRUE(isMemberOf(editor, t.macroB, uuidA1)) << "sanity: the transfer happened";
    EXPECT_EQ(undo.getEditSerial(), serialBefore + 1) << "leave + join + position must be exactly ONE undo step";

    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(isMemberOf(editor, t.macroA, uuidA1)) << "undo puts the module back in A";
    EXPECT_FALSE(isMemberOf(editor, t.macroB, uuidA1)) << "and out of B";
    const auto* macroB = editor.getMacros().find(t.macroB);
    ASSERT_NE(macroB, nullptr);
    EXPECT_EQ(macroB->members.size(), 2u) << "B is unchanged";
    EXPECT_TRUE(macroB->hasMember(uuidB1));
    EXPECT_TRUE(macroB->hasMember(uuidB2));
    const auto a1After = nodeIdForUuid(engine, uuidA1);
    ASSERT_NE(a1After.uid, 0u);
    EXPECT_EQ((int)engine.getGraph().getNodeForId(a1After)->properties["x"], originalX);
    EXPECT_EQ((int)engine.getGraph().getNodeForId(a1After)->properties["y"], originalY);

    ASSERT_TRUE(undo.redo());
    EXPECT_FALSE(isMemberOf(editor, t.macroA, uuidA1)) << "redo replays the leave";
    EXPECT_TRUE(isMemberOf(editor, t.macroB, uuidA1)) << "and the join";
}

// ---------------------------------------------------------------------------------------------
// (c) A cable from the transferred module to an A member: A gains a crossing port, B gets one, no
//     orphan or duplicate ports, and undo removes both.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragTransfer, CableToAMemberSplicesPortsOnBothMacrosAndUndoRemovesBoth) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(2000, 1400);
    const auto t = makeTwoExpandedMacros(editor, engine);
    editor.connectPorts(t.a1, 0, t.a2, 0, /*isMidi=*/false); // a1 -> a2, interior to A
    ASSERT_TRUE(editor.getMacros().find(t.macroA)->ports.empty()) << "sanity: interior cable, no ports yet";
    const int nodesBefore = engine.getGraph().getNodes().size();

    auto* comp = findComponent(editor, t.a1);
    ASSERT_NE(comp, nullptr);
    dragBodyBy(*comp, deltaIntoHull(editor, *comp, t.macroB), kCmdClick);
    ASSERT_TRUE(isMemberOf(editor, t.macroB, uuidOf(engine, t.a1))) << "sanity: the transfer happened";

    // The cable a1->a2 now leaves B and enters A: one crossing port on each macro, nothing else.
    const auto* macroA = editor.getMacros().find(t.macroA);
    const auto* macroB = editor.getMacros().find(t.macroB);
    ASSERT_NE(macroA, nullptr);
    ASSERT_NE(macroB, nullptr);
    EXPECT_EQ(macroA->ports.size(), 1u) << "A gains exactly one crossing port";
    EXPECT_EQ(macroB->ports.size(), 1u) << "B gets exactly one port";
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore + 2) << "two port nodes added, no orphans/duplicates";
    EXPECT_EQ(countMacroPortNodes(engine), 2);

    ASSERT_TRUE(undo.undo());
    EXPECT_TRUE(editor.getMacros().find(t.macroA)->ports.empty()) << "undo removes A's port";
    EXPECT_TRUE(editor.getMacros().find(t.macroB)->ports.empty()) << "and B's";
    EXPECT_EQ(engine.getGraph().getNodes().size(), nodesBefore);
    EXPECT_EQ(countMacroPortNodes(engine), 0);
}

// ---------------------------------------------------------------------------------------------
// (d) The source macro can dissolve (the module was its LAST ordinary member) and the join must
//     still land. A mouse gesture cannot reach this - the leave test needs a non-empty hull left
//     behind - so this calls finalizeMacroMembershipDrag itself with a single-member A.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragTransfer, TransferOutOfASingleMemberMacroStillJoinsWhenTheSourceDissolves) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1400);
    const auto t = makeTwoExpandedMacros(editor, engine);

    // Shrink A to just a1: remove a2 (a plain macro edit, not under test).
    editor.getMacroController().removeSelectionFromMacro(t.macroA, {uuidOf(engine, t.a2)});
    ASSERT_NE(editor.getMacros().find(t.macroA), nullptr);
    ASSERT_EQ(editor.getMacros().find(t.macroA)->members.size(), 1u);

    const auto uuidA1 = uuidOf(engine, t.a1);
    auto* comp = findComponent(editor, t.a1);
    ASSERT_NE(comp, nullptr);
    editor.finalizeMacroMembershipDrag(comp, t.macroA, t.macroB);

    EXPECT_EQ(editor.getMacros().find(t.macroA), nullptr) << "A loses its last member and dissolves";
    EXPECT_TRUE(isMemberOf(editor, t.macroB, uuidA1)) << "the join still lands on B";
}

// ---------------------------------------------------------------------------------------------
// (e) The query itself.
// ---------------------------------------------------------------------------------------------

TEST(MacroDragTransfer, JoinOrLeaveQueryCoversTransferLeaveJoinAndStaying) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1400);
    const auto t = makeTwoExpandedMacros(editor, engine);
    const auto outside = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1500, 900);
    auto& macros = editor.getMacroController();

    const auto hullB = macros.macroHullBounds(t.macroB);
    const auto excludingA1 = macros.macroHullBoundsExcluding(t.macroA, uuidOf(engine, t.a1));
    ASSERT_FALSE(hullB.isEmpty());
    ASSERT_FALSE(excludingA1.isEmpty());

    // {A, B}: a member of A whose centre is in B's hull.
    auto targets = macros.macroDragJoinOrLeaveTarget(t.a1, hullB.getCentre());
    EXPECT_EQ(targets.leave, t.macroA);
    EXPECT_EQ(targets.join, t.macroB);

    // {A, ""}: a member of A whose centre is over no hull at all.
    targets = macros.macroDragJoinOrLeaveTarget(t.a1, {1900, 1300});
    EXPECT_EQ(targets.leave, t.macroA);
    EXPECT_TRUE(targets.join.isEmpty());

    // {"", B}: a non-member over B's hull.
    targets = macros.macroDragJoinOrLeaveTarget(outside, hullB.getCentre());
    EXPECT_TRUE(targets.leave.isEmpty());
    EXPECT_EQ(targets.join, t.macroB);

    // {"", ""}: a non-member over nothing; a member still inside its own macro's excluding hull.
    targets = macros.macroDragJoinOrLeaveTarget(outside, {1900, 1300});
    EXPECT_TRUE(targets.isEmpty());
    targets = macros.macroDragJoinOrLeaveTarget(t.a1, excludingA1.getCentre());
    EXPECT_TRUE(targets.isEmpty()) << "inside A's hull excluding the dragged member: staying";
}

// The trap the JOIN scan must avoid: macroHullAt uses the LIVE hull, which still contains the
// dragged module (so it answers A, or something inside A), and would report the macro being left as
// its own join target. A's hull is made smaller than B's and B is made to overlap A's live hull.
TEST(MacroDragTransfer, JoinScanSkipsTheMacroBeingLeftEvenWhenItsLiveHullIsTheSmallestUnderTheCentre) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(3000, 2000);

    // B is big and encloses A's whole footprint; A is the smaller (smallest-hull-wins) macro.
    const auto a1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1000, 700);
    const auto a2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1000, 1000);
    const auto b1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 600, 100);
    const auto b2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1700, 1500);
    editor.setSelectedNodes({a1, a2});
    const auto macroA = editor.getMacroController().groupSelectionIntoMacro();
    editor.setSelectedNodes({b1, b2});
    const auto macroB = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroA.isEmpty());
    ASSERT_FALSE(macroB.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroA, false);
    editor.getMacroController().setMacroCollapsed(macroB, false);
    auto& macros = editor.getMacroController();

    auto* compA1 = findComponent(editor, a1);
    ASSERT_NE(compA1, nullptr);
    const auto centre = compA1->getBounds().getCentre();
    ASSERT_TRUE(macros.macroHullBounds(macroB).contains(centre)) << "premise: B covers a1's centre";
    ASSERT_TRUE(macros.macroHullBounds(macroA).contains(centre)) << "premise: A's live hull covers it too";
    ASSERT_LT(macros.macroHullBounds(macroA).getWidth() * macros.macroHullBounds(macroA).getHeight(),
              macros.macroHullBounds(macroB).getWidth() * macros.macroHullBounds(macroB).getHeight())
        << "premise: A's hull is the smaller one";
    ASSERT_EQ(macros.macroHullAt(centre), macroA) << "premise: the plain hit-test answers A, the trap";
    ASSERT_FALSE(macros.macroHullBoundsExcluding(macroA, uuidOf(engine, a1)).contains(centre))
        << "premise: the centre is outside A's hull once a1 stops holding it open";

    const auto targets = macros.macroDragJoinOrLeaveTarget(a1, centre);
    EXPECT_EQ(targets.leave, macroA);
    EXPECT_EQ(targets.join, macroB) << "the scan must skip A and find B, not report A as its own target";
}

// ---------------------------------------------------------------------------------------------
// (f) The drag-without-Cmd preference (default ON).
// ---------------------------------------------------------------------------------------------

TEST(MacroDragTransfer, PreferenceDefaultsToOn) {
    AudioEngine engine;
    GraphEditor editor(engine);
    EXPECT_TRUE(editor.getMacroDragWithoutCmdEnabled());
}

TEST(MacroDragTransfer, PreferenceOnPlainDragJoinsAndLeaves) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1400);
    editor.setMacroDragWithoutCmdEnabled(true);
    const auto t = makeTwoExpandedMacros(editor, engine);
    const auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1500, 900);

    // JOIN: a plain drag of an outside module into B.
    editor.setSelectedNodes({c});
    auto* compC = findComponent(editor, c);
    ASSERT_NE(compC, nullptr);
    dragBodyBy(*compC, deltaIntoHull(editor, *compC, t.macroB), kPlainClick,
               [&] { EXPECT_EQ(editor.getMacroDragJoinId(), t.macroB) << "a plain drag arms the join candidate"; });
    EXPECT_TRUE(isMemberOf(editor, t.macroB, uuidOf(engine, c))) << "pref on: a plain drag joins";

    // LEAVE: a plain drag of an A member far outside every hull.
    editor.setSelectedNodes({t.a1});
    auto* compA1 = findComponent(editor, t.a1);
    ASSERT_NE(compA1, nullptr);
    dragBodyBy(*compA1, {0, 1100}, kPlainClick);
    EXPECT_FALSE(isMemberOf(editor, t.macroA, uuidOf(engine, t.a1))) << "pref on: a plain drag leaves";
}

TEST(MacroDragTransfer, PreferenceOffPlainDragNeitherJoinsNorLeaves) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1400);
    editor.setMacroDragWithoutCmdEnabled(false);
    const auto t = makeTwoExpandedMacros(editor, engine);
    const auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1500, 900);

    editor.setSelectedNodes({c});
    auto* compC = findComponent(editor, c);
    ASSERT_NE(compC, nullptr);
    dragBodyBy(*compC, deltaIntoHull(editor, *compC, t.macroB), kPlainClick,
               [&] { EXPECT_FALSE(editor.hasMacroDragCandidate()) << "pref off: no candidate without Cmd"; });
    EXPECT_EQ(editor.getMacroController().macroForNode(c), nullptr) << "pref off: a plain drag does not join";

    editor.setSelectedNodes({t.a1});
    auto* compA1 = findComponent(editor, t.a1);
    ASSERT_NE(compA1, nullptr);
    dragBodyBy(*compA1, {0, 1100}, kPlainClick);
    EXPECT_TRUE(isMemberOf(editor, t.macroA, uuidOf(engine, t.a1))) << "pref off: a plain drag does not leave";

    // Cmd still works with the preference off.
    editor.setSelectedNodes({c});
    dragBodyBy(*findComponent(editor, c), deltaIntoHull(editor, *findComponent(editor, c), t.macroB), kCmdClick);
    EXPECT_TRUE(isMemberOf(editor, t.macroB, uuidOf(engine, c))) << "Cmd joins regardless of the preference";
}

// The trap: a plain press on an already-selected module of a multi-selection keeps the group, and a
// pref-latched reparentArmed would reparent only the grabbed module and skip finalizeSelectionDrag.
TEST(MacroDragTransfer, PreferenceOnNeverReparentsAGroupDragAndStillFinalizesIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1400);
    editor.setMacroDragWithoutCmdEnabled(true);
    const auto t = makeTwoExpandedMacros(editor, engine);
    const auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1500, 900);
    const auto d = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1500, 1200);
    auto* compC = findComponent(editor, c);
    auto* compD = findComponent(editor, d);
    ASSERT_NE(compC, nullptr);
    ASSERT_NE(compD, nullptr);
    const auto offsetBefore = compD->getPosition() - compC->getPosition();

    editor.setSelectedNodes({c, d});
    dragBodyBy(*compC, deltaIntoHull(editor, *compC, t.macroB), kPlainClick, [&] {
        EXPECT_TRUE(editor.isSelectionDragActive()) << "sanity: this is a group drag";
        EXPECT_FALSE(editor.hasMacroDragCandidate()) << "a group drag never arms a reparent candidate";
    });

    EXPECT_FALSE(editor.isSelectionDragActive()) << "finalizeSelectionDrag must have run (no stuck group drag)";
    EXPECT_FALSE(editor.getDragDropController().isDragPreviewActive());
    EXPECT_EQ(editor.getMacroController().macroForNode(c), nullptr) << "the grabbed module must not reparent alone";
    EXPECT_EQ(editor.getMacroController().macroForNode(d), nullptr);
    EXPECT_EQ(compD->getPosition() - compC->getPosition(), offsetBefore) << "the group moved as one rigid body";

    // The same for a group of A's own members dragged out: both stay members.
    editor.setSelectedNodes({t.a1, t.a2});
    auto* compA1 = findComponent(editor, t.a1);
    ASSERT_NE(compA1, nullptr);
    dragBodyBy(*compA1, {0, 1100}, kPlainClick);
    EXPECT_FALSE(editor.isSelectionDragActive());
    EXPECT_TRUE(isMemberOf(editor, t.macroA, uuidOf(engine, t.a1)));
    EXPECT_TRUE(isMemberOf(editor, t.macroA, uuidOf(engine, t.a2)));
}

// macOS Ctrl+drag is the insert-between gesture and must never reparent, preference or not. Only
// expressible where Ctrl and Cmd are distinct bits (see MacroDragMembership's own note).
TEST(MacroDragTransfer, PreferenceOnMacOsCtrlOnlyDragDoesNotReparent) {
    if constexpr (juce::ModifierKeys::commandModifier == juce::ModifierKeys::ctrlModifier) {
        GTEST_SKIP() << "Ctrl and Cmd are the same modifier on this platform.";
    }

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1400);
    editor.setMacroDragWithoutCmdEnabled(true);
    const auto t = makeTwoExpandedMacros(editor, engine);
    const auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1500, 900);
    auto* compC = findComponent(editor, c);
    ASSERT_NE(compC, nullptr);

    const juce::ModifierKeys ctrlOnly(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier);
    dragBodyBy(*compC, deltaIntoHull(editor, *compC, t.macroB), ctrlOnly,
               [&] { EXPECT_FALSE(editor.hasMacroDragCandidate()) << "Ctrl-only must never arm a candidate"; });
    EXPECT_EQ(editor.getMacroController().macroForNode(c), nullptr) << "Ctrl-only must stay insert-between only";
}

// ---------------------------------------------------------------------------------------------
// (h) A library module dropped over an expanded hull joins it (Cmd or the preference), in one undo
//     step with its creation. Modules only; the hull is tested at the ghost's centre.
// ---------------------------------------------------------------------------------------------

namespace {

struct DropFixture {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    TwoMacros macros;
    juce::Component source;

    DropFixture() {
        undo.setGraphEditor(&editor);
        editor.setSize(2000, 1400);
        macros = makeTwoExpandedMacros(editor, engine);
    }

    /** Drops an Oscillator with the cursor at canvas point `p` and returns the new node's uuid ("" if none). */
    juce::String dropAt(juce::Point<int> p) {
        std::set<uint32_t> before;
        for (auto* n : engine.getGraph().getNodes())
            before.insert(n->nodeID.uid);
        editor.itemDropped({juce::var("Oscillator"), &source, p});
        for (auto* n : engine.getGraph().getNodes())
            if (before.count(n->nodeID.uid) == 0)
                return n->properties["uuid"].toString();
        return {};
    }

    juce::Point<int> hullBCentre() { return editor.getMacroController().macroHullBounds(macros.macroB).getCentre(); }
};

} // namespace

TEST(MacroDragTransfer, LibraryDropWithCmdOverAHullJoinsIt) {
    DropFixture f;
    f.editor.setMacroDragWithoutCmdEnabled(false);
    f.editor.setMacroJoinCommandOverrideForTests(true);
    const auto uuid = f.dropAt(f.hullBCentre());
    ASSERT_FALSE(uuid.isEmpty()) << "the dropped module must have a uuid";
    EXPECT_TRUE(isMemberOf(f.editor, f.macros.macroB, uuid));
}

TEST(MacroDragTransfer, LibraryDropWithPreferenceOnAndNoModifierJoinsIt) {
    DropFixture f;
    f.editor.setMacroDragWithoutCmdEnabled(true);
    f.editor.setMacroJoinCommandOverrideForTests(false);
    const auto uuid = f.dropAt(f.hullBCentre());
    ASSERT_FALSE(uuid.isEmpty());
    EXPECT_TRUE(isMemberOf(f.editor, f.macros.macroB, uuid));
}

TEST(MacroDragTransfer, LibraryDropWithNeitherModifierNorPreferenceDoesNotJoin) {
    DropFixture f;
    f.editor.setMacroDragWithoutCmdEnabled(false);
    f.editor.setMacroJoinCommandOverrideForTests(false);
    const int nodesBefore = f.engine.getGraph().getNodes().size();
    f.dropAt(f.hullBCentre());
    ASSERT_EQ(f.engine.getGraph().getNodes().size(), nodesBefore + 1) << "the drop still creates the module";
    EXPECT_EQ(f.editor.getMacros().find(f.macros.macroB)->members.size(), 2u) << "but B is untouched";
}

TEST(MacroDragTransfer, LibraryDropOutsideEveryHullNeverJoinsEvenWithTheModifier) {
    DropFixture f;
    f.editor.setMacroDragWithoutCmdEnabled(true);
    f.editor.setMacroJoinCommandOverrideForTests(true);
    const auto uuid = f.dropAt({1800, 1300});
    ASSERT_FALSE(f.engine.getGraph().getNodes().isEmpty());
    EXPECT_EQ(f.editor.getMacros().findByMember(uuid), nullptr) << "a drop outside any hull is a plain drop";
    EXPECT_EQ(f.editor.getMacros().find(f.macros.macroA)->members.size(), 2u);
    EXPECT_EQ(f.editor.getMacros().find(f.macros.macroB)->members.size(), 2u);
}

TEST(MacroDragTransfer, LibraryDropJoinIsOneUndoStepWithNodeCreation) {
    DropFixture f;
    f.editor.setMacroJoinCommandOverrideForTests(true);
    const int nodesBefore = f.engine.getGraph().getNodes().size();
    const int serialBefore = f.undo.getEditSerial();

    const auto uuid = f.dropAt(f.hullBCentre());
    ASSERT_TRUE(isMemberOf(f.editor, f.macros.macroB, uuid));
    EXPECT_EQ(f.undo.getEditSerial(), serialBefore + 1) << "node creation + membership: ONE undo step";

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.engine.getGraph().getNodes().size(), nodesBefore) << "one undo removes the node";
    const auto* macroB = f.editor.getMacros().find(f.macros.macroB);
    ASSERT_NE(macroB, nullptr);
    EXPECT_EQ(macroB->members.size(), 2u) << "and the membership with it";
    EXPECT_FALSE(macroB->hasMember(uuid));

    ASSERT_TRUE(f.undo.redo());
    EXPECT_EQ(f.engine.getGraph().getNodes().size(), nodesBefore + 1);
    EXPECT_TRUE(isMemberOf(f.editor, f.macros.macroB, uuid)) << "redo brings both back";
}

// The live drop-target highlight: a library drag hovering an expanded hull with the modifier active
// emphasises it through the same join-id path a canvas reparent drag uses, and clears on exit/drop.
TEST(MacroDragTransfer, LibraryDragHighlightsTheJoinTargetAndClearsOnExitAndDrop) {
    DropFixture f;
    f.editor.setMacroDragWithoutCmdEnabled(false);
    f.editor.setMacroJoinCommandOverrideForTests(true);
    const auto details = juce::DragAndDropTarget::SourceDetails(juce::var("Oscillator"), &f.source, f.hullBCentre());

    f.editor.itemDragEnter(details);
    f.editor.itemDragMove(details);
    EXPECT_EQ(f.editor.getMacroDragJoinId(), f.macros.macroB) << "hovering B's hull emphasises B";
    f.editor.itemDragExit(details);
    EXPECT_TRUE(f.editor.getMacroDragJoinId().isEmpty()) << "exit clears the highlight";

    f.editor.itemDragEnter(details);
    f.editor.itemDragMove(details);
    f.editor.itemDropped(details);
    EXPECT_TRUE(f.editor.getMacroDragJoinId().isEmpty()) << "a drop clears the highlight";
    // The real enter/move/drop flow (a live ghost that anti-overlap has moved off the cursor) still
    // joins: the hull is tested where the ghost was AIMED, not where the card finally lands.
    EXPECT_EQ(f.editor.getMacros().find(f.macros.macroB)->members.size(), 3u);

    f.editor.setMacroJoinCommandOverrideForTests(false);
    f.editor.itemDragEnter(details);
    f.editor.itemDragMove(details);
    EXPECT_TRUE(f.editor.getMacroDragJoinId().isEmpty()) << "no modifier and no preference: no highlight";
    f.editor.itemDragExit(details);
}
