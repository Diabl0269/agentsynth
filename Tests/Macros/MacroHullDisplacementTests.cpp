// MacroHullDisplacementTests.cpp
// Neighbours make room when a macro grows: grouping, expanding, adding ports, nesting and a nested group,
// all through the real controller entry points and the app's undo manager (see docs/layout/layout.md, "Making
// room when something grows"). The pure geometry is covered in Tests/UI/Layout/LayoutUtilDisplacementTests.cpp.

#include "MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

namespace {

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
    }

    MacroGroupController& ctl() { return editor.getMacroController(); }

    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }
    NodeID filter(int x, int y) { return addModuleAt(editor, engine, std::make_unique<FilterModule>(), x, y); }

    juce::Rectangle<int> rect(NodeID id) { return findComponent(editor, id)->getBounds(); }

    juce::String uuid(NodeID id) { return uuidOf(engine, id); }

    juce::String group(std::vector<NodeID> ids) {
        editor.setSelectedNodes(ids);
        return ctl().groupSelectionIntoMacro();
    }

    // The rectangle a macro occupies as a layout unit: its hull when open, its card when collapsed.
    juce::Rectangle<int> footprint(const juce::String& macroId) {
        const auto* macro = editor.getMacros().find(macroId);
        return macro->collapsed ? ctl().macroCableAnchorBounds(*macro) : ctl().macroHullBounds(macroId);
    }
};

} // namespace

// Adding a far-away module to an open macro stretches its hull across the canvas: a loose module in between
// is pushed clear.
TEST(MacroHullDisplacement, AddingAModuleToAnOpenMacroPushesTheModulesItsHullNowCovers) {
    Canvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto between = c.osc(400, 700);
    const auto far = c.osc(400, 1200);
    const auto macroId = c.group({m1, m2});
    ASSERT_FALSE(macroId.isEmpty());
    c.ctl().setMacroCollapsed(macroId, false);
    ASSERT_FALSE(c.rect(between).intersects(c.ctl().macroHullBounds(macroId)));
    const auto start = c.rect(between);

    c.ctl().addSelectionToMacro(macroId, {c.uuid(far)});

    EXPECT_TRUE(c.ctl().macroHullBounds(macroId).contains(c.rect(far)));
    EXPECT_NE(c.rect(between).getPosition(), start.getPosition());
    EXPECT_FALSE(c.rect(between).intersects(c.ctl().macroHullBounds(macroId)));
    EXPECT_GE(c.rect(between).getX(), 0);
    EXPECT_GE(c.rect(between).getY(), 0);
}

TEST(MacroHullDisplacement, ExpandingPushesANeighbourOutOfTheGrownHull) {
    Canvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto neighbour = c.osc(c.rect(m2).getRight() + 20, 300);
    const auto macroId = c.group({m1, m2});
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_FALSE(c.rect(neighbour).intersects(c.footprint(macroId))) << "clear while collapsed";

    c.ctl().setMacroCollapsed(macroId, false);

    EXPECT_FALSE(c.rect(neighbour).intersects(c.ctl().macroHullBounds(macroId)));
    EXPECT_GE(c.rect(neighbour).getX(), 0);
    EXPECT_GE(c.rect(neighbour).getY(), 0);
}

// The hull grows 110 px leftward over a neighbour hugging the canvas edge: the neighbour must not be sent off
// the canvas or teleported across the hull; it keeps its x and drops down.
TEST(MacroHullDisplacement, LeftEdgeNeighbourKeepsItsColumnAndMovesDown) {
    Canvas c;
    const auto m1 = c.osc(340, 400);
    const auto m2 = c.osc(640, 400);
    const auto neighbour = c.osc(40, 400);
    const auto start = c.rect(neighbour);
    const auto macroId = c.group({m1, m2});
    ASSERT_FALSE(macroId.isEmpty());
    ASSERT_EQ(c.rect(neighbour), start) << "grouping alone does not touch it";

    c.ctl().setMacroCollapsed(macroId, false);

    const auto after = c.rect(neighbour);
    EXPECT_EQ(after.getX(), start.getX());
    EXPECT_GT(after.getY(), start.getY());
    EXPECT_FALSE(after.intersects(c.ctl().macroHullBounds(macroId)));
}

// Expanding a macro that sits inside an open macro grows the OUTER hull; it is readable straight away (no
// animation tick) and the outer macro's own neighbour has already been cleared.
TEST(MacroHullDisplacement, ExpandingANestedMacroGrowsTheOuterHullAndClearsItsNeighbours) {
    Canvas c;
    const auto a = c.osc(400, 300);
    const auto b = c.osc(700, 300);
    const auto e = c.osc(400, 900);
    const auto outer = c.group({a, b, e});
    ASSERT_FALSE(outer.isEmpty());
    c.ctl().setMacroCollapsed(outer, false);
    const auto inner = c.group({a, b});
    ASSERT_FALSE(inner.isEmpty());
    ASSERT_EQ(c.editor.getMacros().parentOf(inner), outer);
    const auto hullBefore = c.ctl().macroHullBounds(outer);
    const auto neighbour = c.osc(hullBefore.getRight() + 30, 500);
    ASSERT_FALSE(c.rect(neighbour).intersects(hullBefore));

    c.ctl().setMacroCollapsed(inner, false);

    const auto hullAfter = c.ctl().macroHullBounds(outer);
    EXPECT_GT(hullAfter.getRight(), hullBefore.getRight());
    EXPECT_FALSE(c.rect(neighbour).intersects(hullAfter));
}

TEST(MacroHullDisplacement, CollapsingReturnsAPushedNeighbour) {
    Canvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto neighbour = c.osc(c.rect(m2).getRight() + 20, 300);
    const auto home = c.rect(neighbour);
    const auto macroId = c.group({m1, m2});
    c.ctl().setMacroCollapsed(macroId, false);
    ASSERT_NE(c.rect(neighbour).getPosition(), home.getPosition()) << "premise: opening pushed it";

    c.ctl().setMacroCollapsed(macroId, true);

    EXPECT_EQ(c.rect(neighbour), home);
    auto* node = c.engine.getGraph().getNodeForId(neighbour);
    EXPECT_EQ(juce::Point<int>((int)node->properties["x"], (int)node->properties["y"]), home.getPosition())
        << "the node property follows, not just the component";
}

// A neighbour the user has moved since it was pushed stays exactly where they put it.
TEST(MacroHullDisplacement, CollapsingLeavesANeighbourTheUserMoved) {
    Canvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto neighbour = c.osc(c.rect(m2).getRight() + 20, 300);
    const auto macroId = c.group({m1, m2});
    c.ctl().setMacroCollapsed(macroId, false);
    c.ctl().moveUnitBy("n:" + juce::String((juce::int64)neighbour.uid), {0, 48}); // what a drag would do
    c.editor.updateComponents();
    const auto moved = c.rect(neighbour);

    c.ctl().setMacroCollapsed(macroId, true);

    EXPECT_EQ(c.rect(neighbour), moved);
}

// The home spot got taken while the macro was open: the neighbour stays where it is instead of piling on top.
TEST(MacroHullDisplacement, CollapsingKeepsANeighbourWhoseHomeIsOccupied) {
    Canvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto neighbour = c.osc(c.rect(m2).getRight() + 20, 300);
    const auto home = c.rect(neighbour);
    const auto macroId = c.group({m1, m2});
    c.ctl().setMacroCollapsed(macroId, false);
    const auto pushed = c.rect(neighbour);
    const auto squatter = c.osc(1800, 1800);
    c.ctl().moveUnitBy("n:" + juce::String((juce::int64)squatter.uid),
                       home.getPosition() - c.rect(squatter).getPosition());
    c.editor.updateComponents();
    ASSERT_EQ(c.rect(squatter).getPosition(), home.getPosition());

    c.ctl().setMacroCollapsed(macroId, true);

    EXPECT_EQ(c.rect(neighbour), pushed);
}

// Collapsing an inner macro returns what its expansion pushed at the OUTER level too.
TEST(MacroHullDisplacement, CollapsingANestedMacroReturnsNeighboursPushedAtTheParentLevel) {
    Canvas c;
    const auto a = c.osc(400, 300);
    const auto b = c.osc(700, 300);
    const auto e = c.osc(400, 900);
    const auto outer = c.group({a, b, e});
    c.ctl().setMacroCollapsed(outer, false);
    const auto inner = c.group({a, b});
    ASSERT_EQ(c.editor.getMacros().parentOf(inner), outer);
    const auto neighbour = c.osc(c.ctl().macroHullBounds(outer).getRight() + 30, 500);
    const auto home = c.rect(neighbour);

    c.ctl().setMacroCollapsed(inner, false);
    ASSERT_NE(c.rect(neighbour).getPosition(), home.getPosition()) << "premise: the outer hull grew into it";

    c.ctl().setMacroCollapsed(inner, true);

    EXPECT_EQ(c.rect(neighbour), home);
}

// A port that grows the hull pushes the module below; deleting it (the default path, which drops the cable rather
// than splicing it) brings the module back.
TEST(MacroHullDisplacement, DeletingThePortThatPushedAModuleReturnsIt) {
    Canvas c;
    const auto a = c.osc(400, 300);
    const auto b = c.osc(700, 300);
    const auto macroId = c.group({a, b});
    c.ctl().setMacroCollapsed(macroId, false);
    const auto hull = c.ctl().macroHullBounds(macroId);
    const auto below = c.osc(hull.getX() + 140, hull.getBottom() + 60);
    const auto home = c.rect(below);

    int added = 0;
    while (c.rect(below) == home && added < 40) {
        c.ctl().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1,
                             "In " + juce::String(added));
        ++added;
    }
    ASSERT_NE(c.rect(below).getPosition(), home.getPosition()) << "premise: enough ports pushed it";

    for (int i = 0; i < added; ++i)
        c.ctl().deleteBottomMacroPort(macroId, true);

    EXPECT_EQ(c.rect(below), home);
}

// The same, through the splicing delete path.
TEST(MacroHullDisplacement, DeletingThePortThatPushedAModuleReturnsItOnTheSplicePath) {
    Canvas c;
    c.editor.setSpliceCableOnMacroPortDeleteEnabled(true);
    const auto a = c.osc(400, 300);
    const auto b = c.osc(700, 300);
    const auto macroId = c.group({a, b});
    c.ctl().setMacroCollapsed(macroId, false);
    const auto hull = c.ctl().macroHullBounds(macroId);
    const auto below = c.osc(hull.getX() + 140, hull.getBottom() + 60);
    const auto home = c.rect(below);

    int added = 0;
    while (c.rect(below) == home && added < 40) {
        c.ctl().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1,
                             "In " + juce::String(added));
        ++added;
    }
    ASSERT_NE(c.rect(below).getPosition(), home.getPosition());

    for (int i = 0; i < added; ++i)
        c.ctl().deleteBottomMacroPort(macroId, true);

    EXPECT_EQ(c.rect(below), home);
}

// One undo of the collapse brings the pushed state back; redo returns the neighbour again.
TEST(MacroHullDisplacement, OneUndoOfTheCollapseRestoresThePushedNeighbour) {
    Canvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto neighbour = c.osc(c.rect(m2).getRight() + 20, 300);
    const auto home = c.rect(neighbour);
    const auto macroId = c.group({m1, m2});
    c.ctl().setMacroCollapsed(macroId, false);
    const auto pushed = c.rect(neighbour);
    c.undo.clearUndoHistory();

    c.ctl().setMacroCollapsed(macroId, true);
    ASSERT_EQ(c.rect(neighbour), home);

    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.rect(neighbour), pushed);
    EXPECT_FALSE(c.editor.getMacros().find(macroId)->collapsed);

    ASSERT_TRUE(c.undo.redo());
    EXPECT_EQ(c.rect(neighbour), home);
}

TEST(MacroHullDisplacement, AddingPortsPushesAModuleBelowTheHull) {
    Canvas c;
    const auto a = c.osc(400, 300);
    const auto b = c.osc(700, 300);
    const auto macroId = c.group({a, b});
    ASSERT_FALSE(macroId.isEmpty());
    c.ctl().setMacroCollapsed(macroId, false);
    const auto hull = c.ctl().macroHullBounds(macroId);
    const auto below = c.osc(hull.getX() + 140, hull.getBottom() + 60);
    const auto startY = c.rect(below).getY();

    int maxHullBottom = 0;
    for (int i = 0; i < 40; ++i) {
        c.ctl().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1,
                             "In " + juce::String(i));
        maxHullBottom = juce::jmax(maxHullBottom, c.ctl().macroHullBounds(macroId).getBottom());
        EXPECT_FALSE(c.rect(below).intersects(c.ctl().macroHullBounds(macroId))) << "after port " << i;
    }
    EXPECT_GT(maxHullBottom, startY) << "the hull really did grow past the module's original top";
    EXPECT_GT(c.rect(below).getY(), startY);
}

// Nesting two open macros grows a hull around both; a second top-level macro beside them is pushed as one
// rigid unit, hidden members included.
TEST(MacroHullDisplacement, NestingPushesASecondTopLevelMacroAsAWholeUnit) {
    Canvas c;
    const auto a1 = c.osc(200, 300), a2 = c.osc(200, 700);
    const auto b1 = c.osc(800, 300), b2 = c.osc(800, 700);
    const auto d1 = c.osc(1250, 300), d2 = c.osc(1250, 700);
    const auto macroA = c.group({a1, a2});
    const auto macroB = c.group({b1, b2});
    const auto macroD = c.group({d1, d2});
    ASSERT_TRUE(macroA.isNotEmpty() && macroB.isNotEmpty() && macroD.isNotEmpty());
    c.ctl().setMacroCollapsed(macroA, false);
    c.ctl().setMacroCollapsed(macroB, false);

    const auto cardBefore = c.footprint(macroD);
    const auto off1 = c.rect(d1).getPosition() - cardBefore.getPosition();
    const auto off2 = c.rect(d2).getPosition() - cardBefore.getPosition();

    c.ctl().selectMacro(macroA, false);
    c.ctl().selectMacro(macroB, true);
    const auto outer = c.ctl().groupSelectionIntoMacro();
    ASSERT_FALSE(outer.isEmpty());
    ASSERT_EQ(c.editor.getMacros().parentOf(macroA), outer);
    c.ctl().setMacroCollapsed(outer, false);

    const auto cardAfter = c.footprint(macroD);
    EXPECT_NE(cardAfter.getPosition(), cardBefore.getPosition());
    EXPECT_FALSE(cardAfter.intersects(c.ctl().macroHullBounds(outer)));
    EXPECT_EQ(c.rect(d1).getPosition() - cardAfter.getPosition(), off1);
    EXPECT_EQ(c.rect(d2).getPosition() - cardAfter.getPosition(), off2);
}

TEST(MacroHullDisplacement, OneUndoRestoresTheNeighbourAndRedoReappliesTheMove) {
    Canvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto between = c.osc(400, 700);
    const auto far = c.osc(400, 1200);
    const auto macroId = c.group({m1, m2});
    c.ctl().setMacroCollapsed(macroId, false);
    c.undo.clearUndoHistory();
    const auto start = c.rect(between).getPosition();
    auto nodePos = [&] {
        auto* node = c.engine.getGraph().getNodeForId(between);
        return juce::Point<int>((int)node->properties["x"], (int)node->properties["y"]);
    };

    c.ctl().addSelectionToMacro(macroId, {c.uuid(far)});
    const auto pushed = c.rect(between).getPosition();
    ASSERT_NE(pushed, start);
    const auto hullAfterAdd = c.ctl().macroHullBounds(macroId);

    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(nodePos(), start);
    EXPECT_EQ(c.rect(between).getPosition(), start);
    EXPECT_FALSE(c.editor.getMacros().find(macroId)->hasMember(c.uuid(far)));
    EXPECT_NE(c.ctl().macroHullBounds(macroId), hullAfterAdd);

    ASSERT_TRUE(c.undo.redo());
    EXPECT_EQ(nodePos(), pushed);
    EXPECT_EQ(c.rect(between).getPosition(), pushed);
    EXPECT_EQ(c.ctl().macroHullBounds(macroId), hullAfterAdd);
}

// A collapsed macro's hidden members are never layout units: they only ever move with their card.
TEST(MacroHullDisplacement, HiddenMembersOfACollapsedMacroMoveOnlyWithTheirCard) {
    Canvas c;
    const auto e1 = c.osc(400, 300), e2 = c.osc(700, 300);
    const auto d1 = c.osc(1000, 300), d2 = c.osc(1000, 700);
    const auto macroE = c.group({e1, e2});
    const auto macroD = c.group({d1, d2});
    ASSERT_TRUE(macroE.isNotEmpty() && macroD.isNotEmpty());
    const auto cardBefore = c.footprint(macroD);
    const auto off1 = c.rect(d1).getPosition() - cardBefore.getPosition();
    const auto off2 = c.rect(d2).getPosition() - cardBefore.getPosition();

    c.ctl().setMacroCollapsed(macroE, false);

    const auto cardAfter = c.footprint(macroD);
    EXPECT_NE(cardAfter.getPosition(), cardBefore.getPosition()) << "the card was in the way";
    EXPECT_FALSE(cardAfter.intersects(c.ctl().macroHullBounds(macroE)));
    EXPECT_EQ(c.rect(d1).getPosition() - cardAfter.getPosition(), off1);
    EXPECT_EQ(c.rect(d2).getPosition() - cardAfter.getPosition(), off2);
    EXPECT_FALSE(findComponent(c.editor, d1)->isVisible());
}

// A chain of pushes (the hull pushes A, A pushes B, B pushes C) unwinds completely when the macro collapses, even
// though B and C are only free once A has gone home.
TEST(MacroHullDisplacement, CollapsingReturnsAWholePushedChain) {
    Canvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto a = c.osc(c.rect(m2).getRight() + 20, 300);
    const auto b = c.osc(c.rect(a).getRight() + 20, 300);
    const auto d = c.osc(c.rect(b).getRight() + 20, 300);
    const auto homeA = c.rect(a), homeB = c.rect(b), homeD = c.rect(d);
    const auto macroId = c.group({m1, m2});
    c.ctl().setMacroCollapsed(macroId, false);
    ASSERT_NE(c.rect(a).getPosition(), homeA.getPosition());
    ASSERT_NE(c.rect(b).getPosition(), homeB.getPosition()) << "premise: the push cascaded to B";
    ASSERT_NE(c.rect(d).getPosition(), homeD.getPosition()) << "premise: and to C";

    c.ctl().setMacroCollapsed(macroId, true);

    EXPECT_EQ(c.rect(a), homeA);
    EXPECT_EQ(c.rect(b), homeB);
    EXPECT_EQ(c.rect(d), homeD);
}

// Undo and redo restore snapshots: they never run the collapse-time return, so a neighbour ends exactly where the
// snapshot had it and no displacement record survives the restore.
TEST(MacroHullDisplacement, UndoAndRedoNeverRunTheReturnAndLeaveNoRecord) {
    Canvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto neighbour = c.osc(c.rect(m2).getRight() + 20, 300);
    const auto home = c.rect(neighbour);
    const auto macroId = c.group({m1, m2});
    c.ctl().setMacroCollapsed(macroId, false);
    const auto pushed = c.rect(neighbour);
    ASSERT_FALSE(c.editor.getMacros().find(macroId)->displaced.empty()) << "premise: the expand recorded its push";
    c.undo.clearUndoHistory();

    c.ctl().setMacroCollapsed(macroId, true);
    ASSERT_EQ(c.rect(neighbour), home);

    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.rect(neighbour), pushed);
    EXPECT_TRUE(c.editor.getMacros().find(macroId)->displaced.empty());

    ASSERT_TRUE(c.undo.redo());
    EXPECT_EQ(c.rect(neighbour), home);
    EXPECT_TRUE(c.editor.getMacros().find(macroId)->displaced.empty());
}
