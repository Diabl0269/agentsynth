// MacroGroupDropTests.cpp
//
// Dropping a group (several selected modules, macros, or both) into, out of and never inside itself, through the real
// mouse callbacks of the unit that leads the drag
// (docs/macros/menu-and-membership.md#dragging-a-group-into-or-out-of-a-macro).

#include "MacroDragTestHelpers.h"
#include "MacroUnitDragHelpers.h"

#include "AppUndoManager.h"

#include <algorithm>
#include <map>
#include <tuple>

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

using Units = std::pair<std::vector<NodeID>, std::vector<juce::String>>;

Units unitsOf(GroupDropFixture& f, Kind kind) {
    switch (kind) {
    case Kind::module:
        return {{f.m1}, {}};
    case Kind::foldedCard:
        return {{}, {f.fId}};
    case Kind::openMacro:
        return {{}, {f.oId}};
    case Kind::modules:
        return {{f.m1, f.m2}, {}};
    case Kind::macros:
        return {{}, {f.fId, f.oId}};
    case Kind::mixed:
        return {{f.m1}, {f.fId}};
    }
    return {};
}

// Selects the units (a lone unit is selected by its own press) and drags the first one's centre (a module's or folded
// card's) or pointer (an open macro's chip) to `target` through its real mouse callbacks; `midDrag` runs before
// release.
void selectAndDrag(GroupDropFixture& f, const std::vector<NodeID>& modules, const std::vector<juce::String>& macros,
                   juce::Point<int> target, const std::function<void()>& midDrag) {
    const bool groupDrag = modules.size() + macros.size() > 1;
    if (groupDrag) {
        for (auto id : modules)
            shiftClickModule(f.editor, id);
        for (const auto& id : macros)
            shiftClickMacro(f.editor, id);
    }
    if (!modules.empty())
        dragBodyBy(*findComponent(f.editor, modules.front()), target - f.centreOf(modules.front()),
                   groupDrag ? kPlainClick : kCmdClick, midDrag);
    else if (f.ctl().getMacroCardForTest(macros.front())->isVisible())
        dragCardCentreTo(f.editor, macros.front(), target, midDrag);
    else
        dragChipTo(f.editor, macros.front(), target, midDrag);
}

// Every visible card on the canvas (modules and folded macro cards), by component.
std::map<juce::Component*, juce::Rectangle<int>> visibleCards(GroupDropFixture& f) {
    std::map<juce::Component*, juce::Rectangle<int>> cards;
    for (auto* comp : f.editor.getModuleComponents())
        if (comp != nullptr && comp->isVisible())
            cards[comp] = comp->getBounds();
    for (const auto& m : f.editor.getMacros().getAll())
        if (auto* card = f.ctl().getMacroCardForTest(m.id); card != nullptr && card->isVisible())
            cards[card] = card->getBounds();
    return cards;
}

// A card the drag moves: a dragged module, a dragged folded macro's card or an open dragged macro's member.
bool isDragged(GroupDropFixture& f, const std::vector<NodeID>& modules, const std::vector<juce::String>& macros,
               juce::Component* card) {
    if (auto* macroCard = dynamic_cast<MacroCardComponent*>(card))
        return std::find(macros.begin(), macros.end(), macroCard->getMacroId()) != macros.end();
    const auto id = static_cast<ModuleComponent*>(card)->getNodeId();
    if (std::find(modules.begin(), modules.end(), id) != modules.end())
        return true;
    const auto* owner = f.editor.getMacros().findByMember(uuidOf(f.engine, id));
    return owner != nullptr && std::find(macros.begin(), macros.end(), owner->id) != macros.end();
}

// The landing ghost the canvas paints (GraphEditorCables.cpp): a group drag's rects, else the single card's.
std::vector<juce::Rectangle<int>> shownGhosts(GraphEditor& editor) {
    const auto& dragDrop = editor.getDragDropController();
    auto ghosts = dragDrop.getGroupGhosts();
    if (ghosts.empty() && dragDrop.isDragPreviewActive() && !dragDrop.getDragPreviewGhost().isEmpty())
        ghosts.push_back(dragDrop.getDragPreviewGhost());
    return ghosts;
}

std::vector<juce::Rectangle<int>> sorted(std::vector<juce::Rectangle<int>> rects) {
    std::sort(rects.begin(), rects.end(), [](auto a, auto b) {
        return std::make_tuple(a.getX(), a.getY(), a.getWidth(), a.getHeight()) <
               std::make_tuple(b.getX(), b.getY(), b.getWidth(), b.getHeight());
    });
    return rects;
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
        const auto [modules, macros] = unitsOf(f, kind);
        juce::String shownBeforeRelease;
        // The outline a user sees: the join target, or the macro a single module already moved into live.
        selectAndDrag(f, modules, macros, f.gapInB(), [&] {
            shownBeforeRelease = f.editor.getMacroDragJoinId().isNotEmpty() ? f.editor.getMacroDragJoinId()
                                                                            : f.editor.getMacroDragLiveOwnerId();
        });

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

// Regression test for FRO1440: a group drag's ghost showed only the grabbed card, at a spot the group never landed on,
// and a group holding a macro was pushed off its placement by its own room-making. For every group drag kind dropped
// into an open macro (a lone folded card or open macro moves its members as a group), and a plain group move on the
// empty canvas, the ghost rects shown just before the release are exactly where the moved cards land. A single module
// has its own ghost path (dragPreviewGhost_), so it is checked on the very tick it crosses the border too
// (SingleModuleGhostIsRightOnTheTickItCrosses).
TEST(MacroGroupDrop, GhostShowsWhereEveryMovedCardLands) {
    for (const int k : {2, 3, 4, 5, -1}) {
        GroupDropFixture f;
        const bool canvasMove = k < 0;
        SCOPED_TRACE(canvasMove ? "two modules on the canvas" : nameOf(static_cast<Kind>(k)));
        const auto [modules, macros] = canvasMove ? Units{{f.m1, f.m2}, {}} : unitsOf(f, static_cast<Kind>(k));
        const auto target = canvasMove ? f.centreOf(f.m1) + juce::Point<int>(0, -700) : f.gapInB();
        std::vector<juce::Rectangle<int>> ghosts;
        selectAndDrag(f, modules, macros, target, [&] { ghosts = shownGhosts(f.editor); });
        f.editor.finishCardGlideForTest();

        // Where the dragged cards are now (a neighbour the drop makes room for is not one of them).
        std::vector<juce::Rectangle<int>> landed;
        for (const auto& [card, rect] : visibleCards(f))
            if (isDragged(f, modules, macros, card))
                landed.push_back(rect);
        EXPECT_FALSE(ghosts.empty()) << "the drag shows a landing ghost";
        EXPECT_EQ(sorted(ghosts), sorted(landed)) << "the ghost is where the cards land";
        EXPECT_TRUE(shownGhosts(f.editor).empty()) << "the ghost ends with the drop";
    }
}

// Regression test for FRO1491: a single module dragged into an open macro drew its landing ghost one frame late on the
// exact tick it crossed the border, because the ghost was placed before that tick's live join. The ghost shown on the
// crossing tick is where the module lands.
TEST(MacroGroupDrop, SingleModuleGhostIsRightOnTheTickItCrosses) {
    GroupDropFixture f;
    std::vector<juce::Rectangle<int>> ghosts;
    juce::String outlined;
    selectAndDrag(f, {f.m1}, {}, f.gapInB(), [&] {
        ghosts = shownGhosts(f.editor);
        outlined = f.editor.getMacroDragLiveOwnerId();
    });
    f.editor.finishCardGlideForTest();

    EXPECT_EQ(outlined, f.bId) << "the drag has crossed into the macro";
    ASSERT_EQ(ghosts.size(), 1u) << "the drag shows one landing ghost";
    EXPECT_EQ(ghosts.front(), findComponent(f.editor, f.m1)->getBounds()) << "the ghost is where the module lands";
}
