// MacroDropPlacementTests.cpp
// Where a dragged or dropped module lands relative to macros: collapsed cards and open hulls are obstacles (layout
// units), the hidden members of a collapsed macro are not, and a plain drag (no Cmd) over an open hull joins it.
// Every gesture drives the REAL ModuleComponent mouseDown/mouseDrag/mouseUp (see MacroDragTestHelpers.h) and the
// real itemDragMove/itemDropped -- see docs/layout/layout.md#making-room-when-something-grows.

#include "MacroContainer/MacroDragTestHelpers.h"

#include "AppUndoManager.h"
#include "UI/Layout/LayoutUtil.h"
#include <gtest/gtest.h>
#include <set>

namespace {

struct DropCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    DropCanvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
    }

    MacroGroupController& ctl() { return editor.getMacroController(); }
    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }
    NodeID filter(int x, int y) { return addModuleAt(editor, engine, std::make_unique<FilterModule>(), x, y); }
    ModuleComponent& comp(NodeID id) { return *findComponent(editor, id); }
    juce::Rectangle<int> rect(NodeID id) { return comp(id).getBounds(); }

    juce::String group(std::vector<NodeID> ids) {
        editor.setSelectedNodes(ids);
        return ctl().groupSelectionIntoMacro();
    }

    juce::Rectangle<int> card(const juce::String& macroId) {
        return ctl().macroCableAnchorBounds(*editor.getMacros().find(macroId));
    }

    /** A collapsed two-module macro whose card sits at (cx, cy) while its hidden members stay far away. */
    juce::String collapsedMacroWithCardAt(int cx, int cy) {
        const auto m1 = osc(200, 300);
        const auto m2 = osc(700, 300);
        const auto macroId = group({m1, m2});
        editor.getMacros().find(macroId)->bounds.setPosition(cx, cy);
        editor.updateComponents();
        return macroId;
    }

    void dragTo(NodeID id, juce::Point<int> topLeft) {
        dragBodyBy(comp(id), topLeft - comp(id).getPosition(), kPlainClick);
    }
};

} // namespace

// The hidden members of a collapsed macro keep their pre-collapse positions, right under and around the card. They
// are not on the canvas, so a module set just below the card must land exactly where it was put.
TEST(MacroDropPlacement, DragToJustBelowACollapsedCardLandsAtTheSnappedDropSpot) {
    DropCanvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto macroId = c.group({m1, m2});
    ASSERT_TRUE(c.editor.getMacros().find(macroId)->collapsed);
    const auto card = c.card(macroId);
    ASSERT_FALSE(c.comp(m1).isVisible()) << "premise: the member is hidden but still sits under the card";
    const auto loose = c.filter(1800, 1500);

    const auto spot = synth::LayoutUtil::snap({card.getX(), card.getBottom() + 24});
    c.dragTo(loose, spot);

    EXPECT_EQ(c.rect(loose).getPosition(), spot);
}

TEST(MacroDropPlacement, DragOntoACollapsedCardIsPushedClearOfIt) {
    DropCanvas c;
    const auto macroId = c.collapsedMacroWithCardAt(1400, 900);
    const auto card = c.card(macroId);
    ASSERT_FALSE(card.isEmpty());
    const auto loose = c.filter(300, 1700);

    c.dragTo(loose, card.getPosition() + juce::Point<int>(16, 16));

    EXPECT_FALSE(c.rect(loose).intersects(card)) << "a collapsed card is a solid obstacle, not a join target";
    EXPECT_EQ(c.editor.getMacros().find(macroId)->members.size(), 2u) << "and dropping on it does not join";
}

TEST(MacroDropPlacement, SelectionDragNearACollapsedCardIsPushedClearOfIt) {
    DropCanvas c;
    const auto macroId = c.collapsedMacroWithCardAt(1400, 900);
    const auto card = c.card(macroId);
    const auto a = c.filter(300, 1700);
    const auto b = c.filter(300, 2050);
    c.editor.setSelectedNodes({a, b});

    c.dragTo(a, card.getPosition() + juce::Point<int>(16, 16));

    EXPECT_FALSE(c.rect(a).intersects(card));
    EXPECT_FALSE(c.rect(b).intersects(card));
    EXPECT_EQ(c.rect(b).getY() - c.rect(a).getY(), 350) << "still one rigid group";
}

// A module released over an open hull it does not belong to is not thrown out of it: it joins (below), and the
// join is what decides, not the hull acting as an obstacle.
TEST(MacroDropPlacement, PlainDragOfALooseModuleIntoAnOpenHullJoinsWithoutCmd) {
    DropCanvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto macroId = c.group({m1, m2});
    c.ctl().setMacroCollapsed(macroId, false);
    const auto loose = c.filter(2000, 1500);
    const auto target = c.ctl().macroHullBounds(macroId).getCentre() - c.rect(loose).getCentre();

    dragBodyBy(c.comp(loose), target, kPlainClick, [&] {
        EXPECT_EQ(c.editor.getMacroDragJoinId(), macroId) << "the hull is emphasised during a plain drag";
    });

    EXPECT_TRUE(c.editor.getMacros().find(macroId)->hasMember(uuidOf(c.engine, loose)));
    EXPECT_TRUE(c.ctl().macroHullBounds(macroId).contains(c.rect(loose).getCentre()))
        << "it lands inside the hull it joined instead of being pushed out";
}

TEST(MacroDropPlacement, PlainDragOfAMemberOutOfItsHullLeavesIt) {
    DropCanvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto m3 = c.osc(1000, 300);
    const auto macroId = c.group({m1, m2, m3});
    c.ctl().setMacroCollapsed(macroId, false);
    c.editor.setSelectedNodes({m3});

    dragBodyBy(c.comp(m3), {0, 1100}, kPlainClick);

    EXPECT_FALSE(c.editor.getMacros().find(macroId)->hasMember(uuidOf(c.engine, m3)));
    EXPECT_TRUE(c.editor.getMacros().find(macroId)->hasMember(uuidOf(c.engine, m1)));
}

// A library drop with no modifier over an open hull joins it and lands inside it (the ghost is placed after the
// candidate is set, so the hull is not an obstacle to the ghost).
TEST(MacroDropPlacement, LibraryDropWithoutCmdOverAnOpenHullJoinsAndLandsInsideIt) {
    DropCanvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto macroId = c.group({m1, m2});
    c.ctl().setMacroCollapsed(macroId, false);
    c.editor.setMacroJoinCommandOverrideForTests(false);
    juce::Component source;
    const auto centre = c.ctl().macroHullBounds(macroId).getCentre();
    const juce::DragAndDropTarget::SourceDetails details(juce::var("Filter"), &source, centre);
    std::set<uint32_t> before;
    for (auto* n : c.engine.getGraph().getNodes())
        before.insert(n->nodeID.uid);

    c.editor.itemDragEnter(details);
    c.editor.itemDragMove(details);
    EXPECT_EQ(c.editor.getMacroDragJoinId(), macroId);
    c.editor.itemDropped(details);

    NodeID created;
    for (auto* n : c.engine.getGraph().getNodes())
        if (before.count(n->nodeID.uid) == 0)
            created = n->nodeID;
    ASSERT_NE(created.uid, 0u);
    EXPECT_TRUE(c.editor.getMacros().find(macroId)->hasMember(uuidOf(c.engine, created)));
    EXPECT_TRUE(c.ctl().macroHullBounds(macroId).contains(c.rect(created).getCentre()));
}
