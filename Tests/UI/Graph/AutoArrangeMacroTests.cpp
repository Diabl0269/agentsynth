// AutoArrangeMacroTests.cpp
//
// Auto-arrange with macros, on a bare canvas (the `Canvas` fixture of MacroHullDisplacementTests): a collapsed
// macro is one card whose hidden members travel with it, an open macro is arranged inside and placed as one hull, and
// the whole thing is one undo step that also forgets the transient make-room records. The track rows are covered in
// AutoArrangeTests.cpp, the pure layout in Tests/UI/Layout/HierarchicalArrangeTests.cpp.
// (docs/layout/layout.md#auto-arrange)

#include "../../Macros/MacroContainer/MacroContainerTestHelpers.h"
#include "AutoArrangeTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Layout/HierarchicalArrange.h"
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
    NodeID lfo(int x, int y) { return addModuleAt(editor, engine, std::make_unique<LFOModule>(), x, y); }
    // A modulation routing from `source` into the first channel `dest` accepts.
    void modulate(NodeID source, NodeID dest) {
        for (int channel = 1; channel < 8; ++channel)
            if (engine.addModRouting(source, 0, dest, channel) != NodeID{})
                return;
        FAIL() << "no modulation channel accepted";
    }
    juce::Rectangle<int> rect(NodeID id) { return findComponent(editor, id)->getBounds(); }
    juce::String uuid(NodeID id) { return uuidOf(engine, id); }
    void connect(NodeID a, NodeID b) { engine.getGraph().addConnection({{a, 0}, {b, 0}}); }

    juce::String group(std::vector<NodeID> ids) {
        editor.setSelectedNodes(ids);
        return ctl().groupSelectionIntoMacro();
    }

    juce::Rectangle<int> footprint(const juce::String& macroId) {
        const auto* macro = editor.getMacros().find(macroId);
        return macro->collapsed ? ctl().macroCableAnchorBounds(*macro) : ctl().macroHullBounds(macroId);
    }

    autoarrange_test::Snapshot snap() { return autoarrange_test::snapshot(editor, engine.getGraph()); }
};

} // namespace

// Two osc->filter chains, the second boxed into a collapsed macro, plus a loose module.
TEST(AutoArrangeMacros, ACollapsedMacroIsOneCardAndItsHiddenMembersKeepTheirOffsetToIt) {
    Canvas c;
    const auto a1 = c.osc(700, 300);
    const auto a2 = c.filter(1100, 340);
    const auto b1 = c.osc(300, 900);
    const auto b2 = c.filter(800, 1000);
    const auto loose = c.osc(1500, 200);
    c.connect(a1, a2);
    c.connect(b1, b2);
    const auto macroId = c.group({b1, b2});
    ASSERT_FALSE(macroId.isEmpty());
    const auto* macro = c.editor.getMacros().find(macroId);
    ASSERT_TRUE(macro->collapsed);
    const auto card = c.ctl().macroCableAnchorBounds(*macro);
    const auto offsetB1 = c.rect(b1).getPosition() - card.getPosition();
    const auto offsetB2 = c.rect(b2).getPosition() - card.getPosition();

    c.editor.autoArrange();

    const auto after = c.ctl().macroCableAnchorBounds(*c.editor.getMacros().find(macroId));
    EXPECT_NE(after.getPosition(), card.getPosition()) << "premise: the card was not already in place";
    EXPECT_EQ(c.rect(b1).getPosition() - after.getPosition(), offsetB1);
    EXPECT_EQ(c.rect(b2).getPosition() - after.getPosition(), offsetB2);
    EXPECT_EQ(after.getX() % synth::LayoutUtil::kGridSize, 0);
    EXPECT_EQ(after.getY() % synth::LayoutUtil::kGridSize, 0);
    autoarrange_test::expectNoOverlaps(c.editor);
    (void)loose;
}

TEST(AutoArrangeMacros, AnOpenNestedMacroIsArrangedInsideAndTheOuterHullContainsTheInnerOne) {
    Canvas c;
    const auto a = c.osc(900, 300);
    const auto b = c.filter(300, 700);
    const auto d = c.filter(1500, 1100);
    const auto other = c.osc(400, 1500);
    const auto otherSink = c.filter(1200, 1600);
    c.connect(a, b);
    c.connect(b, d);
    c.connect(other, otherSink);
    const auto outer = c.group({a, b, d});
    ASSERT_FALSE(outer.isEmpty());
    c.ctl().setMacroCollapsed(outer, false);
    const auto inner = c.group({a, b});
    ASSERT_FALSE(inner.isEmpty());
    c.ctl().setMacroCollapsed(inner, false);
    ASSERT_EQ(c.editor.getMacros().parentOf(inner), outer);

    c.editor.autoArrange();

    // The interior follows signal flow: a, then b, then d (a sibling of the inner macro).
    const auto innerHull = c.ctl().macroHullBounds(inner);
    const auto outerHull = c.ctl().macroHullBounds(outer);
    EXPECT_LT(c.rect(a).getRight(), c.rect(b).getX());
    EXPECT_TRUE(outerHull.contains(innerHull));
    EXPECT_TRUE(innerHull.contains(c.rect(a)));
    EXPECT_TRUE(innerHull.contains(c.rect(b)));
    EXPECT_FALSE(innerHull.intersects(c.rect(d))) << "the outer macro's own member stays out of the inner hull";
    EXPECT_FALSE(outerHull.intersects(c.rect(other)));
    EXPECT_FALSE(outerHull.intersects(c.rect(otherSink)));
    EXPECT_GE(outerHull.getX(), 0);
    autoarrange_test::expectNoOverlaps(c.editor);

    // The hull the layout budgeted for is the hull the canvas draws: the union of the inner hull and d, grown once.
    EXPECT_EQ(outerHull, synth::LayoutUtil::openMacroHull(innerHull.getUnion(c.rect(d)), 0));
    EXPECT_EQ(innerHull, synth::LayoutUtil::openMacroHull(c.rect(a).getUnion(c.rect(b)), 0));
}

TEST(AutoArrangeMacros, ArrangingTwiceChangesNothingEvenWithMacros) {
    Canvas c;
    const auto a = c.osc(900, 300);
    const auto b = c.filter(300, 700);
    const auto d = c.osc(1500, 1100);
    const auto e = c.filter(1900, 1300);
    c.connect(a, b);
    c.connect(d, e);
    const auto open = c.group({a, b});
    c.ctl().setMacroCollapsed(open, false);
    const auto collapsed = c.group({d, e});
    ASSERT_FALSE(collapsed.isEmpty());

    c.editor.autoArrange();
    const auto first = c.snap();
    c.editor.autoArrange();

    EXPECT_TRUE(first == c.snap());
}

// Three independent chains, the middle one boxed. Expanding and collapsing the box only changes that row's height;
// the rows keep their order, and collapsing it again gives back exactly the first layout.
TEST(AutoArrangeMacros, ExpandingOrCollapsingOneMacroOnlyChangesThatBlocksFootprint) {
    Canvas c;
    const auto a1 = c.osc(300, 300);
    const auto a2 = c.filter(700, 300);
    const auto b1 = c.osc(300, 900);
    const auto b2 = c.filter(700, 900);
    const auto c1 = c.osc(300, 1500);
    const auto c2 = c.filter(700, 1500);
    c.connect(a1, a2);
    c.connect(b1, b2);
    c.connect(c1, c2);
    const auto box = c.group({b1, b2});
    ASSERT_FALSE(box.isEmpty());
    c.editor.autoArrange();
    const auto collapsedLayout = c.snap();
    const auto rowAY = c.rect(a1).getY();
    const auto rowCY = c.rect(c1).getY();

    c.ctl().setMacroCollapsed(box, false);
    c.editor.autoArrange();

    EXPECT_EQ(c.rect(a1).getPosition(), collapsedLayout.comps.at(a1.uid))
        << "the row above the changed block does not move";
    EXPECT_EQ(c.rect(a1).getY(), rowAY);
    EXPECT_LT(c.rect(a1).getY(), c.ctl().macroHullBounds(box).getY());
    EXPECT_LT(c.ctl().macroHullBounds(box).getBottom(), c.rect(c1).getY());
    EXPECT_GE(c.rect(c1).getY(), rowCY) << "the row below only ever moves down to make room";
    autoarrange_test::expectNoOverlaps(c.editor);

    c.ctl().setMacroCollapsed(box, true);
    c.editor.autoArrange();

    // The layout does not remember the expanded detour: every block is back where it was. (The boxed members were
    // arranged inside the hull meanwhile, so only their card is compared.)
    const auto again = c.snap();
    for (const auto id : {a1, a2, c1, c2})
        EXPECT_EQ(again.nodes.at(id.uid), collapsedLayout.nodes.at(id.uid));
    EXPECT_EQ(again.macroBounds.at(box), collapsedLayout.macroBounds.at(box));
}

// One undo restores every node x/y AND the collapsed card's bounds; redo re-applies them.
TEST(AutoArrangeMacros, OneUndoRestoresCollapsedCardBoundsAndNodePositions) {
    Canvas c;
    const auto a = c.osc(1100, 200);
    const auto b1 = c.osc(300, 900);
    const auto b2 = c.filter(800, 1000);
    c.connect(b1, b2);
    const auto box = c.group({b1, b2});
    ASSERT_FALSE(box.isEmpty());
    const auto before = c.snap();

    c.editor.autoArrange();
    const auto after = c.snap();
    ASSERT_NE(before.macroBounds.at(box), after.macroBounds.at(box));
    ASSERT_NE(before.nodes.at(a.uid), after.nodes.at(a.uid));

    ASSERT_TRUE(c.undo.undo());
    EXPECT_TRUE(before == c.snap());
    ASSERT_TRUE(c.undo.redo());
    EXPECT_TRUE(after == c.snap());
}

// Arrange redefines "home": the pushes an earlier expand recorded are forgotten, so a later collapse never drags a
// neighbour back to a spot the layout has already left.
TEST(AutoArrangeMacros, ArrangingClearsTheTransientMakeRoomRecords) {
    Canvas c;
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    const auto neighbour = c.osc(c.rect(m2).getRight() + 20, 300);
    const auto macroId = c.group({m1, m2});
    ASSERT_FALSE(macroId.isEmpty());
    c.ctl().setMacroCollapsed(macroId, false);
    ASSERT_FALSE(c.editor.getMacros().find(macroId)->displaced.empty()) << "premise: opening pushed the neighbour";
    (void)neighbour;

    c.editor.autoArrange();

    const auto* macro = c.editor.getMacros().find(macroId);
    EXPECT_TRUE(macro->displaced.empty());
    EXPECT_FALSE(macro->hasExpandRecord);
}

TEST(AutoArrangeMacros, AnEmptyCanvasArrangesToNothingAndPushesNoUndoStep) {
    Canvas c;
    c.undo.clearUndoHistory();

    c.editor.autoArrange();

    EXPECT_FALSE(c.undo.canUndo());
}

// A loose modulator that only feeds a macro sits right before it in the macro's row.
TEST(AutoArrangeMacros, AModulatorFeedingOnlyAnOpenMacroSitsRightBeforeIt) {
    Canvas c;
    const auto a = c.osc(700, 300);
    const auto b = c.filter(1100, 300);
    const auto far = c.lfo(1900, 1400);
    c.connect(a, b);
    const auto macroId = c.group({a, b});
    ASSERT_FALSE(macroId.isEmpty());
    c.ctl().setMacroCollapsed(macroId, false);
    c.modulate(far, b);

    c.editor.autoArrange();

    autoarrange_test::expectBeforeInRow(c.rect(far), c.ctl().macroHullBounds(macroId), "plain open macro");
}

TEST(AutoArrangeMacros, AModulatorFeedingOnlyACollapsedMacroSitsRightBeforeIt) {
    Canvas c;
    const auto a = c.osc(700, 300);
    const auto b = c.filter(1100, 300);
    const auto far = c.lfo(1900, 1400);
    c.connect(a, b);
    const auto macroId = c.group({a, b});
    ASSERT_FALSE(macroId.isEmpty());
    c.modulate(far, b);

    c.editor.autoArrange();

    autoarrange_test::expectBeforeInRow(
        c.rect(far), c.ctl().macroCableAnchorBounds(*c.editor.getMacros().find(macroId)), "plain collapsed macro");
}

// The same, when the cable into the macro goes through an auto-created port.
TEST(AutoArrangeMacros, AModulatorCabledIntoAMacroPortSitsRightBeforeTheMacro) {
    Canvas c;
    const auto a = c.osc(700, 300);
    const auto b = c.filter(1100, 300);
    const auto far = c.lfo(1900, 1400);
    c.connect(a, b);
    const auto macroId = c.group({a, b});
    ASSERT_FALSE(macroId.isEmpty());
    c.ctl().setMacroCollapsed(macroId, false);
    ASSERT_TRUE(c.ctl().applyProgrammaticConnectionChange(
        true, [&] { return c.engine.getGraph().addConnection({{far, 0}, {b, 1}}); }));
    c.editor.updateComponents();

    c.editor.autoArrange();

    autoarrange_test::expectBeforeInRow(c.rect(far), c.ctl().macroHullBounds(macroId), "port cable, open macro");
}

// A macro fed by a two-stage chain outside it is at column 2: its modulator joins column 1, right before it, not
// column 0.
TEST(AutoArrangeMacros, AModulatorJoinsTheColumnBeforeADeepMacroNotColumnZero) {
    Canvas c;
    const auto source = c.osc(300, 300);
    const auto pre = c.filter(700, 300);
    const auto a = c.filter(1100, 300);
    const auto b = c.filter(1500, 300);
    const auto far = c.lfo(1900, 1400);
    c.connect(source, pre);
    c.connect(pre, a);
    c.connect(a, b);
    const auto macroId = c.group({a, b});
    ASSERT_FALSE(macroId.isEmpty());
    c.ctl().setMacroCollapsed(macroId, false);
    c.modulate(far, b);

    c.editor.autoArrange();

    autoarrange_test::expectBeforeInRow(c.rect(far), c.ctl().macroHullBounds(macroId), "deep open macro");
    EXPECT_EQ(c.rect(far).getX(), c.rect(pre).getX()) << "the column right before the macro";
    EXPECT_GT(c.rect(far).getX(), c.rect(source).getX());
}

// The LFO takes a cable from the macro's member (retrigger input) and modulates another member: lifted to the macro
// block that is macro -> LFO and LFO -> macro. The LFO still sits one column left of the macro, level with it, and what
// follows the macro keeps its column.
TEST(AutoArrangeMacros, AModulatorThatAlsoTakesInputFromItsMacroStaysBeforeIt) {
    Canvas c;
    const auto keys = addModuleAt(c.editor, c.engine, std::make_unique<MidiKeyboardModule>(), 300, 300);
    const auto a = c.osc(700, 300);
    const auto b = c.filter(1100, 300);
    const auto after = c.filter(1500, 300);
    const auto lfo = c.lfo(1900, 1400);
    const auto midi = juce::AudioProcessorGraph::midiChannelIndex;
    c.connect(a, b);
    c.connect(b, after);
    const auto macroId = c.group({keys, a, b});
    ASSERT_FALSE(macroId.isEmpty());
    c.ctl().setMacroCollapsed(macroId, false);
    ASSERT_TRUE(c.engine.getGraph().addConnection({{keys, midi}, {lfo, midi}}));
    c.modulate(lfo, b);

    c.editor.autoArrange();

    const auto hull = c.ctl().macroHullBounds(macroId);
    autoarrange_test::expectBeforeInRow(c.rect(lfo), hull, "cycle");
    EXPECT_GT(c.rect(after).getX(), hull.getRight()) << "the module after the macro stays right of it";
    const auto first = c.snap();
    c.editor.autoArrange();
    EXPECT_TRUE(first == c.snap());
}
