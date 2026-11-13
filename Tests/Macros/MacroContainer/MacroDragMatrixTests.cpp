// MacroDragMatrixTests.cpp
//
// The drag matrix: every unit the canvas drags (a module, a folded macro card, an open macro by its name chip) crossed
// with every membership change a drag can make (join an open macro, leave its own, transfer from one to another, stay
// inside), each through the real mouse callbacks of that unit, each one undo step. A membership mechanism built for
// one unit and missing on a sibling shows up here as a failed row (docs/macros/menu-and-membership.md).

#include "MacroDragTestHelpers.h"
#include "MacroUnitDragHelpers.h"

#include "AppUndoManager.h"

namespace {

enum class Unit { module, foldedCard, openChip };
enum class Move { join, leave, transfer, stay };

const char* nameOf(Unit u) {
    return u == Unit::module ? "module" : u == Unit::foldedCard ? "folded macro card" : "open macro chip";
}

const char* nameOf(Move m) {
    return m == Move::join ? "join" : m == Move::leave ? "leave" : m == Move::transfer ? "transfer" : "stay";
}

// Open macro A {a1, a2, + the unit}, open macro B {b1, b2} on the right with a wide empty gap, and a loose unit below
// B. The unit is a module, or a macro {x1, x2} folded or open.
struct MatrixFixture {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    Unit unit;
    NodeID insideModule, looseModule;
    juce::String aId, bId, insideMacro, looseMacro;

    explicit MatrixFixture(Unit u)
        : unit(u) {
        undo.setGraphEditor(&editor);
        editor.setSize(4400, 3600);
        const auto a1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 300);
        const auto a2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 300, 1000);
        const auto b1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 2200, 300);
        const auto b2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 3700, 300);
        std::vector<NodeID> aMembers{a1, a2};
        if (unit == Unit::module) {
            insideModule = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 800, 300);
            looseModule = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 2200, 2400);
            aMembers.push_back(insideModule);
        } else {
            const auto x1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 800, 300);
            const auto x2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1200, 300);
            insideMacro = group({x1, x2});
            aMembers.insert(aMembers.end(), {x1, x2});
            const auto l1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 2200, 2400);
            const auto l2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 2600, 2400);
            looseMacro = group({l1, l2});
        }
        aId = group(aMembers);
        bId = group({b1, b2});
        for (const auto& id : {aId, bId})
            ctl().setMacroCollapsed(id, false);
        if (unit == Unit::openChip)
            for (const auto& id : {insideMacro, looseMacro})
                ctl().setMacroCollapsed(id, false);
        editor.clearSelection();
        editor.updateComponents();
        editor.finishCardGlideForTest();
        undo.clearUndoHistory();
    }

    MacroGroupController& ctl() { return editor.getMacroController(); }
    juce::String group(const std::vector<NodeID>& nodes) {
        editor.setSelectedNodes(nodes);
        return ctl().groupSelectionIntoMacro();
    }

    // The macro that holds the dragged unit (a module's owner, a macro's parent), empty at the top level.
    juce::String ownerOf(bool loose) {
        if (unit != Unit::module)
            return editor.getMacros().parentOf(loose ? looseMacro : insideMacro);
        const auto* owner = editor.getMacros().findByMember(uuidOf(engine, loose ? looseModule : insideModule));
        return owner != nullptr ? owner->id : juce::String();
    }

    juce::Point<int> gapInB() {
        const auto hull = ctl().macroHullBounds(bId);
        return {hull.getCentreX(), hull.getY() + 200};
    }

    juce::Point<int> belowA() {
        const auto hull = ctl().macroHullBounds(aId);
        return {hull.getCentreX(), hull.getBottom() + 450};
    }

    // Where the unit's drag point is now: a module's or a folded card's centre, an open macro's chip.
    juce::Point<int> grabPoint(bool loose) {
        if (unit == Unit::module)
            return findComponent(editor, loose ? looseModule : insideModule)->getBounds().getCentre();
        const auto& id = loose ? looseMacro : insideMacro;
        if (unit == Unit::foldedCard)
            return ctl().getMacroCardForTest(id)->getBounds().getCentre();
        return ctl().macroChipBounds(id).getCentre();
    }

    // Drags the unit with its real mouse callbacks so its drag point lands on `target`.
    void drag(bool loose, juce::Point<int> target) {
        if (unit == Unit::module)
            dragBodyBy(*findComponent(editor, loose ? looseModule : insideModule), target - grabPoint(loose),
                       kCmdClick);
        else if (unit == Unit::foldedCard)
            dragCardCentreTo(editor, loose ? looseMacro : insideMacro, target);
        else
            dragChipTo(editor, loose ? looseMacro : insideMacro, target);
    }
};

} // namespace

// Regression test for FRO1161: a macro dragged by its card or chip could join an open macro but never leave one, while
// a module could do both. Every unit must make every move, each one undo step.
TEST(MacroDragMatrix, EveryDragUnitJoinsLeavesTransfersAndStays) {
    for (const auto unit : {Unit::module, Unit::foldedCard, Unit::openChip})
        for (const auto move : {Move::join, Move::leave, Move::transfer, Move::stay}) {
            SCOPED_TRACE(juce::String(nameOf(unit)) + " / " + nameOf(move));
            MatrixFixture f(unit);
            const bool loose = move == Move::join;
            ASSERT_EQ(f.ownerOf(loose), loose ? juce::String() : f.aId);
            const auto before = f.ownerOf(loose);

            juce::Point<int> target;
            juce::String expected;
            switch (move) {
            case Move::join:
            case Move::transfer:
                target = f.gapInB();
                expected = f.bId;
                break;
            case Move::leave:
                target = f.belowA();
                break;
            case Move::stay:
                target = f.grabPoint(loose) + juce::Point<int>(0, 40);
                expected = f.aId;
                break;
            }
            f.drag(loose, target);

            EXPECT_EQ(f.ownerOf(loose), expected);
            EXPECT_TRUE(f.ctl().draggedMacroUnit().isEmpty());
            ASSERT_TRUE(f.undo.undo());
            EXPECT_EQ(f.ownerOf(loose), before) << "one Cmd+Z puts it back";
            EXPECT_FALSE(f.undo.canUndo()) << "the whole drag was one step";
        }
}

namespace {

const juce::ModifierKeys kShiftClick(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);

// A real Shift-click on the canvas (an open macro's chip or its empty space): the editor's own mouseDown and mouseUp.
void shiftClickCanvas(GraphEditor& editor, juce::Point<int> point) {
    editor.mouseDown(realMouseEvent(editor, point, point, kShiftClick));
    editor.mouseUp(realMouseEvent(editor, point, point, kShiftClick));
}

// Shift-clicks a macro the way a user adds it to the selection: its folded card's body, or its open name chip.
void shiftClickMacro(MatrixFixture& f, const juce::String& macroId) {
    if (auto* card = f.ctl().getMacroCardForTest(macroId); card != nullptr && card->isVisible()) {
        const juce::Point<int> body(140, 60);
        card->mouseDown(realMouseEvent(*card, body, body, kShiftClick));
        card->mouseUp(realMouseEvent(*card, body, body, kShiftClick));
        return;
    }
    shiftClickCanvas(f.editor, f.ctl().macroChipBounds(macroId).getCentre());
}

// Where a macro sits: a folded one's stored card position, an open one's border.
juce::Point<int> macroPos(MatrixFixture& f, const juce::String& macroId) {
    const auto* macro = f.editor.getMacros().find(macroId);
    return macro->collapsed ? macro->bounds.getPosition() : f.ctl().macroHullBounds(macroId).getPosition();
}

juce::Point<int> loosePos(MatrixFixture& f) {
    return f.unit == Unit::module ? findComponent(f.editor, f.looseModule)->getPosition() : macroPos(f, f.looseMacro);
}

} // namespace

// Regression test for FRO1150: with several macros selected, pressing one macro's card or chip collapsed the selection
// to that macro, so only it moved. The multi-macro row: every drag unit, with a second macro (folded or open) also
// selected, carries that macro by the same delta, keeps the selection and changes no membership, in one undo step.
TEST(MacroDragMatrix, EveryDragUnitCarriesTheOtherSelectedMacrosAsOneUndoStep) {
    for (const auto unit : {Unit::module, Unit::foldedCard, Unit::openChip})
        for (const bool otherFolded : {true, false}) {
            SCOPED_TRACE(juce::String(nameOf(unit)) + (otherFolded ? " + folded macro" : " + open macro"));
            MatrixFixture f(unit);
            if (otherFolded) {
                f.ctl().setMacroCollapsed(f.bId, true);
                f.editor.finishCardGlideForTest();
                f.undo.clearUndoHistory();
            }
            if (unit == Unit::module) {
                auto* comp = findComponent(f.editor, f.looseModule);
                const juce::Point<int> body(comp->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
                comp->mouseDown(realMouseEvent(*comp, body, body, kShiftClick));
                comp->mouseUp(realMouseEvent(*comp, body, body, kShiftClick));
            } else {
                shiftClickMacro(f, f.looseMacro);
            }
            shiftClickMacro(f, f.bId);
            ASSERT_TRUE(f.ctl().isMacroInSelection(f.bId));
            if (unit != Unit::module)
                ASSERT_TRUE(f.ctl().isMacroInSelection(f.looseMacro));

            const auto looseStart = loosePos(f);
            const auto bStart = macroPos(f, f.bId);
            const juce::Point<int> delta(0, 400);
            if (unit == Unit::module)
                dragBodyBy(*findComponent(f.editor, f.looseModule), delta, kPlainClick);
            else
                f.drag(/*loose=*/true, f.grabPoint(true) + delta);

            const auto moved = loosePos(f) - looseStart;
            EXPECT_NE(moved, juce::Point<int>()) << "the dragged unit moved";
            EXPECT_EQ(macroPos(f, f.bId) - bStart, moved)
                << "the other selected macro moved with it, by the same delta";
            if (otherFolded)
                EXPECT_EQ(f.ctl().getMacroCardForTest(f.bId)->getPosition(), macroPos(f, f.bId)) << "its card too";
            EXPECT_TRUE(f.ctl().isMacroInSelection(f.bId)) << "the selection was kept";
            EXPECT_TRUE(f.ownerOf(true).isEmpty()) << "a group drag changes no membership";
            EXPECT_TRUE(f.editor.getMacros().parentOf(f.bId).isEmpty());
            EXPECT_TRUE(f.ctl().draggedMacroUnit().isEmpty());

            ASSERT_TRUE(f.undo.undo());
            EXPECT_EQ(loosePos(f), looseStart);
            EXPECT_EQ(macroPos(f, f.bId), bStart) << "one Cmd+Z puts both back";
            EXPECT_FALSE(f.undo.canUndo()) << "the whole drag was one step";
        }
}

// Regression test for FRO1152: Shift-click on an open macro's empty space started a selection box and dropped the
// selection. It now adds that macro to the selection (and a second Shift-click takes it out), like its name chip.
TEST(MacroDragMatrix, ShiftClickOnAnOpenMacrosEmptySpaceTogglesItInTheSelection) {
    MatrixFixture f(Unit::module);
    f.editor.setSelectedNodes({f.looseModule});
    const auto bHull = f.ctl().macroHullBounds(f.bId);
    const auto gap = f.gapInB();
    ASSERT_EQ(f.ctl().macroHullAt(gap), f.bId);
    ASSERT_TRUE(f.ctl().macroChipAt(gap).isEmpty());

    shiftClickCanvas(f.editor, gap);
    EXPECT_TRUE(f.ctl().isMacroInSelection(f.bId)) << "the open macro joined the selection";
    EXPECT_TRUE(f.editor.isNodeSelected(f.looseModule)) << "what was selected stays selected";

    shiftClickCanvas(f.editor, gap);
    EXPECT_FALSE(f.ctl().isMacroInSelection(f.bId)) << "a second Shift-click takes it out";
    EXPECT_TRUE(f.editor.isNodeSelected(f.looseModule));

    // A Shift-drag from there is still a selection box (replacing, as anywhere on the canvas): it does not toggle.
    const auto from = gap;
    const juce::Point<int> to(bHull.getRight() - 10, bHull.getBottom() - 10);
    f.editor.mouseDown(realMouseEvent(f.editor, from, from, kShiftClick));
    f.editor.mouseDrag(realMouseEvent(f.editor, to, from, kShiftClick, true));
    f.editor.mouseUp(realMouseEvent(f.editor, to, from, kShiftClick, true));
    EXPECT_FALSE(f.editor.isNodeSelected(f.looseModule)) << "a box drag replaces the selection";
}
