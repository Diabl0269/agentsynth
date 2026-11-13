// MacroDropPlacementTests.cpp
// Where a dragged or dropped module lands relative to macros: collapsed cards and open hulls are obstacles (layout
// units), the hidden members of a collapsed macro are not, and a plain drag (no Cmd) over an open hull joins it.
// Every gesture drives the REAL ModuleComponent mouseDown/mouseDrag/mouseUp (see MacroDragTestHelpers.h) and the
// real itemDragMove/itemDropped -- see docs/layout/layout.md#making-room-when-something-grows.

#include "MacroContainer/MacroDragTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/SamplerModule.h"
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
    NodeID sampler(int x, int y) { return addModuleAt(editor, engine, std::make_unique<SamplerModule>(), x, y); }
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
        EXPECT_EQ(c.editor.getMacroDragLiveOwnerId(), macroId) << "it joins, emphasised, during a plain drag";
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

namespace {
// Members this close to the top make expanding nudge the hull down into the canvas.
struct NearTopMacro {
    DropCanvas c;
    NodeID m1, m2;
    juce::String macroId;
    juce::Rectangle<int> card;

    NearTopMacro() {
        m1 = c.osc(100, 20);
        m2 = c.osc(400, 20);
        macroId = c.group({m1, m2});
        card = c.card(macroId);
    }
};
} // namespace

// Expanding nudged the members into the canvas; collapsing puts the card back where it was, so a module dropped
// right under the card returns to its own spot instead of staying pushed.
TEST(MacroDropPlacement, CollapseAfterACanvasNudgedExpandRestoresTheCardAndReturnsTheNeighbour) {
    NearTopMacro t;
    auto& c = t.c;
    const auto loose = c.filter(1800, 1500);
    const auto spot = synth::LayoutUtil::snap({t.card.getX(), t.card.getBottom() + 24});
    c.dragTo(loose, spot);
    ASSERT_EQ(c.rect(loose).getPosition(), spot);

    c.ctl().setMacroCollapsed(t.macroId, false);
    ASSERT_NE(c.rect(t.m1).getY(), 20) << "premise: expanding nudged the members down";
    ASSERT_NE(c.rect(loose).getPosition(), spot) << "premise: the hull pushed the module";

    c.ctl().setMacroCollapsed(t.macroId, true);

    EXPECT_EQ(c.card(t.macroId), t.card);
    EXPECT_EQ(c.rect(t.m1).getY(), 20);
    EXPECT_EQ(c.rect(loose).getPosition(), spot);
}

// Members the user moved after expanding are not snapped back: the card seeds at their union.
TEST(MacroDropPlacement, CollapseAfterMovingAMemberKeepsTheSeededCard) {
    NearTopMacro t;
    auto& c = t.c;
    c.ctl().setMacroCollapsed(t.macroId, false);
    c.ctl().moveUnitBy("n:" + juce::String((juce::int64)t.m1.uid), {48, 0});
    c.ctl().moveUnitBy("n:" + juce::String((juce::int64)t.m2.uid), {48, 0});
    c.editor.updateComponents();
    const auto expected = juce::Point<int>(std::min(c.rect(t.m1).getX(), c.rect(t.m2).getX()),
                                           std::min(c.rect(t.m1).getY(), c.rect(t.m2).getY()));

    c.ctl().setMacroCollapsed(t.macroId, true);

    EXPECT_EQ(c.card(t.macroId).getPosition(), expected);
}

TEST(MacroDropPlacement, OneUndoOfTheCollapseRestoresTheExpandedState) {
    NearTopMacro t;
    auto& c = t.c;
    c.ctl().setMacroCollapsed(t.macroId, false);
    const auto expandedM1 = c.rect(t.m1);
    const auto hull = c.ctl().macroHullBounds(t.macroId);
    c.undo.clearUndoHistory();

    c.ctl().setMacroCollapsed(t.macroId, true);
    ASSERT_TRUE(c.undo.undo());

    EXPECT_FALSE(c.editor.getMacros().find(t.macroId)->collapsed);
    EXPECT_EQ(c.rect(t.m1), expandedM1);
    EXPECT_EQ(c.ctl().macroHullBounds(t.macroId), hull);
}

// Joining an open macro by a plain drag grows its hull and pushes a neighbour; dragging the module back out shrinks
// the hull and the neighbour goes home. The members sit far enough apart for the joiner to land between them, and
// the joiner is taller than the hull, so the hull grows downward into the card just under it.
TEST(MacroDropPlacement, PlainDragJoinPushesANeighbourAndPlainDragOutReturnsIt) {
    DropCanvas c;
    const auto m1 = c.osc(700, 300);
    const auto m2 = c.osc(1000, 300);
    const auto macroId = c.group({m1, m2});
    c.ctl().setMacroCollapsed(macroId, false);
    const auto hull = c.ctl().macroHullBounds(macroId);
    // Neighbours on three sides: where the joiner settles (and so which way the hull grows) depends on the
    // joiner's size, and every side must give way to a hull that grows toward it.
    const std::vector<NodeID> neighbours{c.osc(hull.getRight() + 20, 300), c.osc(hull.getX() - 280 - 20, 300),
                                         c.osc(700, hull.getBottom() + 20)};
    std::vector<juce::Rectangle<int>> homes;
    for (const auto n : neighbours)
        homes.push_back(c.rect(n));
    const auto joiner = c.filter(1000, 1300);
    ASSERT_FALSE(c.editor.getMacros().find(macroId)->hasMember(uuidOf(c.engine, joiner)));

    c.editor.setSelectedNodes({joiner});
    const auto into = c.ctl().macroHullBounds(macroId).getCentre() - c.rect(joiner).getCentre();
    dragBodyBy(c.comp(joiner), into, kPlainClick);
    ASSERT_TRUE(c.editor.getMacros().find(macroId)->hasMember(uuidOf(c.engine, joiner))) << "premise: it joined";
    bool pushed = false;
    for (size_t i = 0; i < neighbours.size(); ++i)
        pushed = pushed || c.rect(neighbours[i]).getPosition() != homes[i].getPosition();
    ASSERT_TRUE(pushed) << "premise: the grown hull pushed a neighbour";

    c.editor.setSelectedNodes({joiner});
    dragBodyBy(c.comp(joiner), {0, 1500}, kPlainClick);

    EXPECT_FALSE(c.editor.getMacros().find(macroId)->hasMember(uuidOf(c.engine, joiner)));
    for (size_t i = 0; i < neighbours.size(); ++i)
        EXPECT_EQ(c.rect(neighbours[i]), homes[i]) << "neighbour " << i;
}
