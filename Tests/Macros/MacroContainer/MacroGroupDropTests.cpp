// MacroGroupDropTests.cpp
//
// Dropping a group (several selected modules, macros, or both) into, out of and never inside itself, through the real
// mouse callbacks of the unit that leads the drag
// (docs/macros/menu-and-membership.md#dragging-a-group-into-or-out-of-a-macro).

#include "MacroDragTestHelpers.h"
#include "MacroUnitDragHelpers.h"

#include "AppUndoManager.h"

namespace {

const juce::ModifierKeys kShift(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);

void shiftClickModule(GraphEditor& editor, NodeID id) {
    auto* comp = findComponent(editor, id);
    ASSERT_NE(comp, nullptr);
    const juce::Point<int> body(comp->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    comp->mouseDown(realMouseEvent(*comp, body, body, kShift));
    comp->mouseUp(realMouseEvent(*comp, body, body, kShift));
}

// A folded macro by its card body, an open one by its name chip: the ways a user adds a macro to the selection.
void shiftClickMacro(GraphEditor& editor, const juce::String& macroId) {
    auto& ctl = editor.getMacroController();
    if (auto* card = ctl.getMacroCardForTest(macroId); card != nullptr && card->isVisible()) {
        const juce::Point<int> body(140, 60);
        card->mouseDown(realMouseEvent(*card, body, body, kShift));
        card->mouseUp(realMouseEvent(*card, body, body, kShift));
        return;
    }
    const auto chip = ctl.macroChipBounds(macroId).getCentre();
    editor.mouseDown(realMouseEvent(editor, chip, chip, kShift));
    editor.mouseUp(realMouseEvent(editor, chip, chip, kShift));
}

// Open macro B {b1, b2} with a wide gap, and below it the loose things a drag can carry: modules m1 and m2, a folded
// macro F and an open macro O.
struct GroupDropFixture {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID m1, m2;
    juce::String bId, fId, oId;

    GroupDropFixture() {
        undo.setGraphEditor(&editor);
        editor.setSize(4800, 3600);
        const auto b1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 2200, 300);
        const auto b2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 3700, 300);
        m1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 2400);
        m2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 800, 2400);
        const auto f1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1400, 2400);
        const auto f2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1800, 2400);
        const auto o1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 2600, 2400);
        const auto o2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 3000, 2400);
        bId = group({b1, b2});
        fId = group({f1, f2});
        oId = group({o1, o2});
        ctl().setMacroCollapsed(bId, false);
        ctl().setMacroCollapsed(oId, false);
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
    juce::String ownerOf(NodeID id) {
        const auto* owner = editor.getMacros().findByMember(uuidOf(engine, id));
        return owner != nullptr ? owner->id : juce::String();
    }
    juce::Point<int> gapInB() {
        const auto hull = ctl().macroHullBounds(bId);
        return {hull.getCentreX(), hull.getY() + 200};
    }
    juce::Point<int> centreOf(NodeID id) { return findComponent(editor, id)->getBounds().getCentre(); }
};

enum class Kind { module, foldedCard, openMacro, modules, macros, mixed };

const char* nameOf(Kind k) {
    switch (k) {
    case Kind::module:
        return "single module";
    case Kind::foldedCard:
        return "single folded macro card";
    case Kind::openMacro:
        return "single open macro";
    case Kind::modules:
        return "two modules";
    case Kind::macros:
        return "folded + open macro";
    case Kind::mixed:
        return "module + folded macro";
    }
    return "";
}

} // namespace

// Regression test for FRO1440: only a single card's drag joined an open macro; a multi-selection dropped on one stayed
// outside. Every drag kind, single or group, dropped on an open macro ends inside it, the drag outlines that macro
// before the release (what the drag shows is what the drop does), and one Cmd+Z puts everything back.
TEST(MacroGroupDrop, EveryDragKindJoinsOrNestsInAnOpenMacro) {
    for (const auto kind :
         {Kind::module, Kind::foldedCard, Kind::openMacro, Kind::modules, Kind::macros, Kind::mixed}) {
        SCOPED_TRACE(nameOf(kind));
        GroupDropFixture f;
        std::vector<NodeID> modules;
        std::vector<juce::String> macros;
        switch (kind) {
        case Kind::module:
            modules = {f.m1};
            break;
        case Kind::foldedCard:
            macros = {f.fId};
            break;
        case Kind::openMacro:
            macros = {f.oId};
            break;
        case Kind::modules:
            modules = {f.m1, f.m2};
            break;
        case Kind::macros:
            macros = {f.fId, f.oId};
            break;
        case Kind::mixed:
            modules = {f.m1};
            macros = {f.fId};
            break;
        }
        const bool groupDrag = modules.size() + macros.size() > 1;
        if (groupDrag) {
            for (auto id : modules)
                shiftClickModule(f.editor, id);
            for (const auto& id : macros)
                shiftClickMacro(f.editor, id);
        }

        juce::String shownBeforeRelease;
        // The outline a user sees: the join target, or the macro a single module already moved into live.
        const auto watch = [&] {
            shownBeforeRelease = f.editor.getMacroDragJoinId().isNotEmpty() ? f.editor.getMacroDragJoinId()
                                                                            : f.editor.getMacroDragLiveOwnerId();
        };
        const auto target = f.gapInB();
        if (!modules.empty())
            dragBodyBy(*findComponent(f.editor, modules.front()), target - f.centreOf(modules.front()),
                       groupDrag ? kPlainClick : kCmdClick, watch);
        else if (f.ctl().getMacroCardForTest(macros.front())->isVisible())
            dragCardCentreTo(f.editor, macros.front(), target, watch);
        else
            dragChipTo(f.editor, macros.front(), target, watch);

        EXPECT_EQ(shownBeforeRelease, f.bId) << "the drag outlines the macro it drops into";
        for (auto id : modules)
            EXPECT_EQ(f.ownerOf(id), f.bId) << "the module joined";
        for (const auto& id : macros)
            EXPECT_EQ(f.editor.getMacros().parentOf(id), f.bId) << "the macro nested";
        EXPECT_TRUE(f.editor.getMacroDragJoinId().isEmpty()) << "the outline ends with the drop";

        ASSERT_TRUE(f.undo.undo());
        for (auto id : modules)
            EXPECT_TRUE(f.ownerOf(id).isEmpty()) << "one Cmd+Z takes every module back out";
        for (const auto& id : macros)
            EXPECT_TRUE(f.editor.getMacros().parentOf(id).isEmpty()) << "one Cmd+Z takes every macro back out";
        EXPECT_FALSE(f.undo.canUndo()) << "the whole drop was one step";
    }
}

// Regression test for FRO1440: two of an open macro's modules dragged together out onto the empty canvas leave it, as
// one module dragged out does; the macro keeps its other member, and one Cmd+Z puts both back in.
TEST(MacroGroupDrop, GroupDraggedOutOfAMacroLeavesIt) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    undo.setGraphEditor(&editor);
    editor.setSize(4400, 3600);
    const auto a1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 300);
    const auto a2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 800, 300);
    const auto a3 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1300, 300);
    editor.setSelectedNodes({a1, a2, a3});
    auto& ctl = editor.getMacroController();
    const auto aId = ctl.groupSelectionIntoMacro();
    ctl.setMacroCollapsed(aId, false);
    editor.clearSelection();
    editor.updateComponents();
    editor.finishCardGlideForTest();
    undo.clearUndoHistory();

    shiftClickModule(editor, a1);
    shiftClickModule(editor, a2);
    const auto hull = ctl.macroHullBounds(aId);
    const auto from = findComponent(editor, a1)->getBounds().getCentre();
    bool leavingShown = false;
    dragBodyBy(*findComponent(editor, a1), juce::Point<int>(from.x, hull.getBottom() + 600) - from, kPlainClick,
               [&] { leavingShown = ctl.isGroupDropLeaving(aId); });

    const auto ownerOf = [&](NodeID id) {
        const auto* owner = editor.getMacros().findByMember(uuidOf(engine, id));
        return owner != nullptr ? owner->id : juce::String();
    };
    EXPECT_TRUE(leavingShown) << "the drag outlines the macro it leaves";
    EXPECT_TRUE(ownerOf(a1).isEmpty());
    EXPECT_TRUE(ownerOf(a2).isEmpty());
    EXPECT_EQ(ownerOf(a3), aId) << "the member left behind stays";

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(ownerOf(a1), aId);
    EXPECT_EQ(ownerOf(a2), aId) << "one Cmd+Z puts both back";
    EXPECT_FALSE(undo.canUndo());
}

// Regression test for FRO1440: a group holding an open macro P never drops into P or a macro inside it (that would be a
// cycle): its own border and its child's are no target, so the drag moves the group and changes no membership.
TEST(MacroGroupDrop, GroupNeverDropsIntoAMacroItCarries) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    undo.setGraphEditor(&editor);
    editor.setSize(4400, 3600);
    auto& ctl = editor.getMacroController();
    const auto p1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 300);
    const auto c1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 800, 300);
    const auto c2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1200, 300);
    const auto m = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 300, 2400);
    editor.setSelectedNodes({c1, c2});
    const auto cId = ctl.groupSelectionIntoMacro();
    ctl.setMacroCollapsed(cId, false);
    editor.setSelectedNodes({p1, c1, c2});
    const auto pId = ctl.groupSelectionIntoMacro();
    ctl.setMacroCollapsed(pId, false);
    ctl.setMacroCollapsed(cId, false);
    editor.clearSelection();
    editor.updateComponents();
    editor.finishCardGlideForTest();
    undo.clearUndoHistory();
    ASSERT_EQ(editor.getMacros().parentOf(cId), pId);

    shiftClickMacro(editor, pId);
    shiftClickModule(editor, m);
    ASSERT_TRUE(ctl.isMacroInSelection(pId));
    juce::String overChild = "unset", overSelf = "unset";
    dragChipBy(editor, pId, {0, 300}, [&] {
        overChild = ctl.groupDropTargetAt(ctl.macroHullBounds(cId).getCentre());
        overSelf = ctl.groupDropTargetAt(ctl.macroChipBounds(pId).getCentre());
    });

    EXPECT_TRUE(overChild.isEmpty()) << "a child of a dragged macro is no target";
    EXPECT_TRUE(overSelf.isEmpty()) << "a dragged macro is no target for itself";
    EXPECT_TRUE(editor.getMacros().parentOf(pId).isEmpty());
    EXPECT_EQ(editor.getMacros().parentOf(cId), pId);
    EXPECT_EQ(editor.getMacros().findByMember(uuidOf(engine, m)), nullptr);
    EXPECT_TRUE(undo.canUndo()) << "the group still moved";
}
