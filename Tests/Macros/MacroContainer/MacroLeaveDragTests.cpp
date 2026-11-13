// MacroLeaveDragTests.cpp
//
// Dragging a nested macro OUT of its parent
// (docs/macros/menu-and-membership.md#dropping-a-macro-into-another-or-out-of-its-parent), by its
// folded card or its open name chip, through the real MacroCardComponent and GraphEditor mouse callbacks. While the
// drag runs, the parent's border stays where it was at press and is outlined as the macro to leave; on release the
// macro hangs from the grandparent (or the top level), walking out through every level in one gesture, the ports on
// each border splice as for a module leaving a macro, a transfer lands in the other macro, Esc puts everything back,
// and one Cmd+Z undoes the whole drop.

#include "MacroNestedTestFixture.h"
#include "MacroUnitDragHelpers.h"

#include "AppUndoManager.h"

namespace {

juce::String parentOf(GraphEditor& editor, const juce::String& id) { return editor.getMacros().parentOf(id); }

bool isMember(GraphEditor& editor, AudioEngine& engine, const juce::String& macroId, NodeID node) {
    const auto* macro = editor.getMacros().find(macroId);
    return macro != nullptr && macro->hasMember(uuidOf(engine, node));
}

bool connected(AudioEngine& engine, NodeID from, NodeID to) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == from && c.destination.nodeID == to)
            return true;
    return false;
}

// A point on empty canvas to the right of `macroId`'s border, level with its middle.
juce::Point<int> rightOf(GraphEditor& editor, const juce::String& macroId, int gap = 600) {
    const auto hull = editor.getMacroController().macroHullBounds(macroId);
    return {hull.getRight() + gap, hull.getCentreY()};
}

// Three levels: grandchild G {g1, g2} inside C (G plus c1) inside P (C plus p1, p2), all open. p2 sits far right, so P
// has empty space of its own to the right of C.
struct ThreeLevelFixture {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID g1, g2, c1, p1, p2;
    juce::String gId, cId, pId;

    ThreeLevelFixture() {
        undo.setGraphEditor(&editor);
        editor.setSize(4000, 3000);
        g1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 400, 300);
        g2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 800, 300);
        c1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 400, 1000);
        p1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 400, 1700);
        p2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 2000, 1700);
        editor.setSelectedNodes({g1, g2});
        gId = ctl().groupSelectionIntoMacro();
        editor.setSelectedNodes({g1, g2, c1});
        cId = ctl().groupSelectionIntoMacro();
        editor.setSelectedNodes({g1, g2, c1, p1, p2});
        pId = ctl().groupSelectionIntoMacro();
        editor.clearSelection();
        for (const auto& id : {pId, cId, gId})
            ctl().setMacroCollapsed(id, false);
        editor.updateComponents();
        undo.clearUndoHistory();
    }

    MacroGroupController& ctl() { return editor.getMacroController(); }
    bool linked() { return parentOf(editor, gId) == cId && parentOf(editor, cId) == pId; }
};

} // namespace

// Regression test for FRO1161: a nested macro could not be dragged out of its parent; the parent's border followed it.
TEST(MacroLeaveDrag, AFoldedNestedCardDraggedOutLeavesItsParentAsOneUndoStep) {
    NestedMacroFixture n;
    ASSERT_TRUE(n.linked);
    n.editor.setSize(3200, 2400);
    n.ctl().setMacroCollapsed(n.childId, true);
    n.editor.finishCardGlideForTest();
    n.undo.clearUndoHistory();
    const auto parentAtPress = n.ctl().macroHullBounds(n.parentId);
    const auto cardStart = n.editor.getMacros().find(n.childId)->bounds;
    const auto outside = rightOf(n.editor, n.parentId);

    juce::String leaveMid, joinMid;
    juce::Rectangle<int> paintedMid, liveMid;
    dragCardCentreTo(n.editor, n.childId, outside, [&] {
        leaveMid = n.ctl().macroLeaveCandidate();
        joinMid = n.editor.getMacroDragJoinId();
        paintedMid = n.editor.paintedMacroHullBounds(n.parentId);
        liveMid = n.ctl().macroHullBounds(n.parentId);
    });

    EXPECT_EQ(leaveMid, n.parentId) << "the parent is outlined as the macro to leave";
    EXPECT_TRUE(joinMid.isEmpty());
    EXPECT_NE(liveMid, parentAtPress) << "positive control: the live union does follow the card";
    EXPECT_EQ(paintedMid, parentAtPress) << "the drawn border stays where it was at press";
    EXPECT_TRUE(parentOf(n.editor, n.childId).isEmpty()) << "the macro now sits at the top level";
    EXPECT_TRUE(isMember(n.editor, n.engine, n.parentId, n.p1) && isMember(n.editor, n.engine, n.parentId, n.p2))
        << "the parent keeps its other members";
    EXPECT_FALSE(n.ctl().macroHullBounds(n.parentId).contains(n.editor.getMacros().find(n.childId)->bounds.getCentre()))
        << "the parent's border shrank back";
    EXPECT_TRUE(n.ctl().macroLeaveCandidate().isEmpty());
    EXPECT_TRUE(n.ctl().draggedMacroUnit().isEmpty());

    ASSERT_TRUE(n.undo.undo());
    EXPECT_EQ(parentOf(n.editor, n.childId), n.parentId);
    EXPECT_EQ(n.editor.getMacros().find(n.childId)->bounds.getPosition(), cardStart.getPosition());
    EXPECT_FALSE(n.undo.canUndo()) << "the move and the leave were one step";
    ASSERT_TRUE(n.undo.redo());
    EXPECT_TRUE(parentOf(n.editor, n.childId).isEmpty());
}

TEST(MacroLeaveDrag, AnOpenNestedMacroDraggedOutByItsChipLeavesItsParent) {
    NestedMacroFixture n;
    ASSERT_TRUE(n.linked);
    n.editor.setSize(3200, 2400);
    const auto c1Start = n.pos(n.c1);
    const auto parentAtPress = n.ctl().macroHullBounds(n.parentId);

    juce::Rectangle<int> paintedMid;
    dragChipTo(n.editor, n.childId, rightOf(n.editor, n.parentId),
               [&] { paintedMid = n.editor.paintedMacroHullBounds(n.parentId); });

    EXPECT_EQ(paintedMid, parentAtPress);
    EXPECT_TRUE(parentOf(n.editor, n.childId).isEmpty());
    EXPECT_FALSE(n.editor.getMacros().find(n.childId)->collapsed) << "it stays open";
    EXPECT_FALSE(n.ctl().macroHullBounds(n.parentId).intersects(n.ctl().macroHullBounds(n.childId)));

    ASSERT_TRUE(n.undo.undo());
    EXPECT_EQ(parentOf(n.editor, n.childId), n.parentId);
    EXPECT_EQ(n.pos(n.c1), c1Start);
    EXPECT_FALSE(n.undo.canUndo());
}

TEST(MacroLeaveDrag, DraggedPastEveryBorderItWalksOutOfTwoLevelsInOneStep) {
    ThreeLevelFixture t;
    ASSERT_TRUE(t.linked());
    t.ctl().setMacroCollapsed(t.gId, true);
    t.editor.finishCardGlideForTest();
    t.undo.clearUndoHistory();

    dragCardCentreTo(t.editor, t.gId, rightOf(t.editor, t.pId, 700));

    EXPECT_TRUE(parentOf(t.editor, t.gId).isEmpty()) << "out of C and out of P";
    EXPECT_EQ(parentOf(t.editor, t.cId), t.pId) << "C stays where it was, with c1";
    EXPECT_TRUE(isMember(t.editor, t.engine, t.cId, t.c1));
    ASSERT_TRUE(t.undo.undo());
    EXPECT_TRUE(t.linked());
    EXPECT_FALSE(t.undo.canUndo());
}

TEST(MacroLeaveDrag, DraggedIntoTheGrandparentsOwnSpaceItLeavesOnlyOneLevel) {
    ThreeLevelFixture t;
    ASSERT_TRUE(t.linked());
    t.ctl().setMacroCollapsed(t.gId, true);
    t.editor.finishCardGlideForTest();
    t.undo.clearUndoHistory();
    const auto cHull = t.ctl().macroHullBounds(t.cId);
    const juce::Point<int> inP(cHull.getRight() + 400, cHull.getY() + 250);
    ASSERT_TRUE(t.ctl().macroHullBounds(t.pId).contains(inP));
    ASSERT_EQ(t.ctl().macroHullAt(inP), t.pId) << "empty space of P's own";

    juce::String leaveMid, joinMid;
    dragCardCentreTo(t.editor, t.gId, inP, [&] {
        leaveMid = t.ctl().macroLeaveCandidate();
        joinMid = t.editor.getMacroDragJoinId();
    });

    EXPECT_EQ(leaveMid, t.cId);
    EXPECT_EQ(joinMid, t.pId);
    EXPECT_EQ(parentOf(t.editor, t.gId), t.pId);
    EXPECT_EQ(parentOf(t.editor, t.cId), t.pId);
    ASSERT_TRUE(t.undo.undo());
    EXPECT_TRUE(t.linked());
    EXPECT_FALSE(t.undo.canUndo());
}

TEST(MacroLeaveDrag, ANestedMacroDroppedOnAnotherMacroTransfersWithItsPortsSpliced) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    undo.setGraphEditor(&editor);
    editor.setSize(4000, 2600);
    const auto n1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 300);
    const auto n2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 300);
    const auto a1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 1000);
    const auto a2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 1000);
    const auto b1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1900, 300);
    const auto b2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 3300, 300);
    ASSERT_TRUE(engine.getGraph().addConnection({{n2, 0}, {a2, 0}}));
    auto& ctl = editor.getMacroController();
    editor.setSelectedNodes({n1, n2});
    const auto nId = ctl.groupSelectionIntoMacro(/*autoCreatePorts=*/true);
    editor.setSelectedNodes({n1, n2, a1, a2});
    const auto aId = ctl.groupSelectionIntoMacro();
    editor.setSelectedNodes({b1, b2});
    const auto bId = ctl.groupSelectionIntoMacro();
    editor.clearSelection();
    for (const auto& id : {aId, bId})
        ctl.setMacroCollapsed(id, false);
    editor.finishCardGlideForTest();
    undo.clearUndoHistory();
    ASSERT_EQ(parentOf(editor, nId), aId);
    ASSERT_EQ(editor.getMacros().find(nId)->ports.size(), 1u) << "the cable leaves N through an outlet";
    ASSERT_TRUE(editor.getMacros().find(aId)->ports.empty());
    ASSERT_TRUE(editor.getMacros().find(bId)->ports.empty());

    const auto bHull = ctl.macroHullBounds(bId);
    const juce::Point<int> gapInB(bHull.getCentreX(), bHull.getY() + 200);
    ASSERT_EQ(ctl.macroHullAt(gapInB), bId);
    juce::String leaveMid, joinMid;
    dragCardCentreTo(editor, nId, gapInB, [&] {
        leaveMid = ctl.macroLeaveCandidate();
        joinMid = editor.getMacroDragJoinId();
    });

    EXPECT_EQ(leaveMid, aId);
    EXPECT_EQ(joinMid, bId);
    EXPECT_EQ(parentOf(editor, nId), bId);
    EXPECT_TRUE(isMember(editor, engine, aId, a1) && isMember(editor, engine, aId, a2));
    const auto* a = editor.getMacros().find(aId);
    const auto* b = editor.getMacros().find(bId);
    ASSERT_EQ(a->ports.size(), 1u) << "the cable now enters A through a new inlet";
    EXPECT_TRUE(a->ports.front().isInput);
    ASSERT_EQ(b->ports.size(), 1u) << "and leaves B through a new outlet";
    EXPECT_FALSE(b->ports.front().isInput);
    const auto aIn = nodeIdForUuid(engine, a->ports.front().nodeUuid);
    const auto bOut = nodeIdForUuid(engine, b->ports.front().nodeUuid);
    const auto nOut = nodeIdForUuid(engine, editor.getMacros().find(nId)->ports.front().nodeUuid);
    EXPECT_TRUE(connected(engine, nOut, bOut) && connected(engine, bOut, aIn) && connected(engine, aIn, a2))
        << "the signal still reaches a2, through every border it crosses";

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(parentOf(editor, nId), aId);
    EXPECT_TRUE(editor.getMacros().find(aId)->ports.empty());
    EXPECT_TRUE(editor.getMacros().find(bId)->ports.empty());
    EXPECT_FALSE(undo.canUndo());
}

TEST(MacroLeaveDrag, LeavingSplicesAPortOntoTheParentForACableToAMemberThatStays) {
    NestedMacroFixture n(/*autoCreateChildPorts=*/true, [](NestedMacroFixture& fx) {
        ASSERT_TRUE(fx.engine.getGraph().addConnection({{fx.c1, 0}, {fx.p2, 0}}));
    });
    ASSERT_TRUE(n.linked);
    n.editor.setSize(3200, 2400);
    const auto* child = n.editor.getMacros().find(n.childId);
    ASSERT_EQ(child->ports.size(), 1u);
    const auto childOut = nodeIdForUuid(n.engine, child->ports.front().nodeUuid);
    ASSERT_TRUE(connected(n.engine, childOut, n.p2));
    ASSERT_TRUE(n.editor.getMacros().find(n.parentId)->ports.empty());

    dragChipTo(n.editor, n.childId, rightOf(n.editor, n.parentId));

    ASSERT_TRUE(parentOf(n.editor, n.childId).isEmpty());
    const auto* parent = n.editor.getMacros().find(n.parentId);
    ASSERT_EQ(parent->ports.size(), 1u);
    EXPECT_TRUE(parent->ports.front().isInput);
    const auto parentIn = nodeIdForUuid(n.engine, parent->ports.front().nodeUuid);
    EXPECT_TRUE(connected(n.engine, childOut, parentIn) && connected(n.engine, parentIn, n.p2));
    EXPECT_FALSE(connected(n.engine, childOut, n.p2));

    ASSERT_TRUE(n.undo.undo());
    EXPECT_TRUE(n.editor.getMacros().find(n.parentId)->ports.empty());
    EXPECT_TRUE(connected(n.engine, childOut, n.p2));
}

TEST(MacroLeaveDrag, EscMidDragPutsEverythingBackAndRecordsNothing) {
    for (const bool folded : {false, true}) {
        SCOPED_TRACE(folded ? "folded card" : "open chip");
        NestedMacroFixture n;
        ASSERT_TRUE(n.linked);
        n.editor.setSize(3200, 2400);
        if (folded) {
            n.ctl().setMacroCollapsed(n.childId, true);
            n.editor.finishCardGlideForTest();
        }
        n.undo.clearUndoHistory();
        const auto c1Start = n.pos(n.c1);
        const auto cardStart = n.card(n.childId)->getPosition();
        const auto outside = rightOf(n.editor, n.parentId);

        bool escHandled = false;
        juce::String leaveAfterEsc = "unset";
        const auto pressEsc = [&] {
            ASSERT_EQ(n.ctl().macroLeaveCandidate(), n.parentId) << "the leave was armed before Esc";
            escHandled = n.editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            leaveAfterEsc = n.ctl().macroLeaveCandidate();
        };
        if (folded)
            dragCardCentreTo(n.editor, n.childId, outside, pressEsc);
        else
            dragChipTo(n.editor, n.childId, outside, pressEsc);

        EXPECT_TRUE(escHandled);
        EXPECT_TRUE(leaveAfterEsc.isEmpty()) << "the outline goes at once";
        EXPECT_EQ(parentOf(n.editor, n.childId), n.parentId) << "nothing left the parent";
        EXPECT_EQ(n.pos(n.c1), c1Start);
        EXPECT_EQ(n.card(n.childId)->getPosition(), cardStart);
        EXPECT_FALSE(n.undo.canUndo()) << "a cancelled drag records nothing";
        EXPECT_TRUE(n.ctl().draggedMacroUnit().isEmpty());
        EXPECT_FALSE(n.editor.isMacroChipDragActive());
    }
}

TEST(MacroLeaveDrag, AParentWithNothingElseInsideIsCarriedRatherThanLeft) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    undo.setGraphEditor(&editor);
    editor.setSize(3200, 2400);
    const auto c1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 300);
    const auto c2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 300);
    auto& ctl = editor.getMacroController();
    editor.setSelectedNodes({c1, c2});
    const auto childId = ctl.groupSelectionIntoMacro();
    // A parent that holds only the child: nest it by hand (Cmd+G on one unit refuses).
    synth::Macro wrapper;
    wrapper.name = "Wrapper";
    wrapper.collapsed = false;
    const auto parentId = editor.getMacros().add(wrapper);
    ASSERT_TRUE(editor.getMacros().setParent(childId, parentId));
    ctl.setMacroCollapsed(childId, false);
    editor.updateComponents();
    undo.clearUndoHistory();

    juce::String leaveMid = "unset";
    dragChipTo(editor, childId, rightOf(editor, parentId), [&] { leaveMid = ctl.macroLeaveCandidate(); });

    EXPECT_TRUE(leaveMid.isEmpty()) << "nothing else is inside: there is no body to leave";
    EXPECT_EQ(parentOf(editor, childId), parentId);
}
