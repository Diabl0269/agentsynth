// GraphEditorCanvasTickTests.cpp
//
// What the 30 Hz canvas tick, a pan and a repeat paint cost, as work counts rather than timings
// (docs/layout/rendering.md#per-frame-work-does-not-grow-with-the-patch): the tick keeps the cable memo and refreshes
// the values cables are drawn with in place, refits the canvas frame only after something moved, a pan rebuilds no
// cable, and a paint measures no macro border that has not moved since the last one. Every change that moves a cable
// end still reaches the memo: a card moving, a control moving inside a card, a graph edit made behind the editor's
// back, a routing appearing.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include "UI/Layout/CableCurve.h"
#include <gtest/gtest.h>

namespace {

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID osc, filter;

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
        osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 200, 300);
        filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 300);
        engine.getGraph().addConnection({{osc, 0}, {filter, 0}});
        editor.updateComponents();
        settle();
    }
    void tick() { static_cast<juce::Timer&>(editor).timerCallback(); }
    void paint() { (void)editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f); }
    // Ticks and paints until nothing is left over from setup, then reads the memo once.
    void settle() {
        editor.finishCardGlideForTest();
        editor.finishHullGlideForTest();
        for (int i = 0; i < 2; ++i) {
            tick();
            paint();
        }
        (void)editor.buildVisibleCables();
    }
    int rebuilds() { return editor.getCableRebuildCountForTest(); }
    juce::String groupIntoOpenMacro(std::initializer_list<NodeID> ids) {
        editor.setSelectedNodes(ids);
        const auto id = editor.getMacroController().groupSelectionIntoMacro();
        editor.getMacroController().setMacroCollapsed(id, false);
        settle();
        return id;
    }
};

const graph_editor_types::VisibleCable* cableOfKind(GraphEditor& editor, graph_editor_types::VisibleCable::Kind kind) {
    for (const auto& c : editor.buildVisibleCables())
        if (c.kind == kind)
            return &c;
    return nullptr;
}

} // namespace

TEST(GraphEditorCanvasTick, AnIdleTickAndItsPaintRebuildNoCable) {
    Canvas c;
    const int before = c.rebuilds();
    for (int i = 0; i < 5; ++i) {
        c.tick();
        c.paint();
    }
    (void)c.editor.buildVisibleCables();
    EXPECT_EQ(c.rebuilds(), before) << "an idle frame redraws the cables it already has";
    EXPECT_EQ(c.editor.getVisibleCableCount(), 1);
}

TEST(GraphEditorCanvasTick, ATickRefreshesTheAttenuverterAmountInPlace) {
    Canvas c;
    const auto lfo = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 200, 900);
    const auto atten = c.engine.addModRouting(lfo, 0, c.filter, 1);
    ASSERT_NE(atten.uid, 0u);
    c.editor.updateComponents();
    c.settle();
    ASSERT_NE(cableOfKind(c.editor, graph_editor_types::VisibleCable::Kind::AttenuverterChain), nullptr);

    auto* amount = findParameterByID(c.engine.getGraph().getNodeForId(atten)->getProcessor(), "amount");
    ASSERT_NE(amount, nullptr);
    amount->setValueNotifyingHost(0.25f);
    const int before = c.rebuilds();
    c.tick();
    const auto* chain = cableOfKind(c.editor, graph_editor_types::VisibleCable::Kind::AttenuverterChain);
    ASSERT_NE(chain, nullptr);
    EXPECT_NEAR(chain->attenAmount, -0.5f, 1.0e-4f) << "the knob on the cable follows the amount";
    EXPECT_EQ(c.rebuilds(), before) << "without rebuilding the cables";
}

TEST(GraphEditorCanvasTick, ANewRoutingIsDrawnOnTheNextTick) {
    Canvas c;
    const auto lfo = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 200, 900);
    c.settle();
    const int cables = c.editor.getVisibleCableCount();
    ASSERT_NE(c.engine.addModRouting(lfo, 0, c.filter, 1).uid, 0u);
    c.tick();
    EXPECT_EQ(c.editor.getVisibleCableCount(), cables + 1);
}

TEST(GraphEditorCanvasTick, ACableAddedBehindTheEditorsBackIsDrawnOnceTheGraphSaysSo) {
    Canvas c;
    const auto other = addModuleAt(c.editor, c.engine, std::make_unique<FilterModule>(), 900, 900);
    c.settle();
    const int cables = c.editor.getVisibleCableCount();
    c.engine.getGraph().addConnection({{c.osc, 0}, {other, 0}});   // no repaintCanvas(), no updateComponents()
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // the graph's change broadcast is asynchronous
    c.tick();
    EXPECT_EQ(c.editor.getVisibleCableCount(), cables + 1);
}

TEST(GraphEditorCanvasTick, ACardThatMovesTakesItsCablesWithIt) {
    Canvas c;
    auto* card = findComponent(c.editor, c.filter);
    const auto end = c.editor.buildVisibleCables().front().p2;
    card->setTopLeftPosition(card->getPosition() + juce::Point<int>(120, 80));
    const auto moved = c.editor.buildVisibleCables().front().p2;
    EXPECT_NEAR(moved.x - end.x, 120.0f, 0.5f);
    EXPECT_NEAR(moved.y - end.y, 80.0f, 0.5f);
}

TEST(GraphEditorCanvasTick, AControlThatMovesInsideACardDropsTheCableMemo) {
    Canvas c;
    auto* card = findComponent(c.editor, c.filter);
    juce::Slider* knob = nullptr;
    for (auto* child : card->getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child); s != nullptr && knob == nullptr)
            knob = s;
    ASSERT_NE(knob, nullptr);
    const int before = c.rebuilds();
    knob->setBounds(knob->getBounds().translated(0, 6)); // what the on-card layout editor does to a knob
    (void)c.editor.buildVisibleCables();
    EXPECT_EQ(c.rebuilds(), before + 1) << "a knob landing moves with its knob";
}

TEST(GraphEditorCanvasTick, TheTickRefitsTheCanvasFrameOnlyAfterSomethingMoved) {
    Canvas c;
    graph_editor_paint::workCounters() = {};
    c.tick();
    c.tick();
    EXPECT_EQ(graph_editor_paint::workCounters().canvasFrameFits, 0) << "nothing moved";

    auto* card = findComponent(c.editor, c.filter);
    card->setTopLeftPosition(card->getPosition() + juce::Point<int>(400, 0));
    c.tick();
    c.tick();
    EXPECT_EQ(graph_editor_paint::workCounters().canvasFrameFits, 1) << "one fit for the move";
}

TEST(GraphEditorCanvasTick, APanRebuildsNoCableAndAZoomStillDoes) {
    Canvas c;
    c.groupIntoOpenMacro({c.osc, c.filter}); // an open macro's port jacks slide with the zoom
    const int before = c.rebuilds();
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 0.2f;
    wheel.isSmooth = true;
    const auto centre = c.editor.getLocalBounds().getCentre().toFloat();
    c.editor.mouseWheelMove(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre, {}, 0.0f, 0.0f,
                                             0.0f, 0.0f, 0.0f, &c.editor, &c.editor, juce::Time::getCurrentTime(),
                                             centre, juce::Time::getCurrentTime(), 1, false),
                            wheel);
    (void)c.editor.buildVisibleCables();
    EXPECT_EQ(c.rebuilds(), before) << "a pan moves no cable end";

    c.editor.zoomAroundCentre(-1.0f);
    (void)c.editor.buildVisibleCables();
    EXPECT_EQ(c.rebuilds(), before + 1);
    c.editor.settleZoomNowForTest();
}

TEST(GraphEditorCanvasTick, ARepaintMeasuresNoMacroBorderUntilAMemberMoves) {
    Canvas c;
    const auto farOsc = addModuleAt(c.editor, c.engine, std::make_unique<OscillatorModule>(), 2000, 1500);
    const auto farFilter = addModuleAt(c.editor, c.engine, std::make_unique<FilterModule>(), 2600, 1500);
    ASSERT_FALSE(c.groupIntoOpenMacro({c.osc, c.filter}).isEmpty());
    ASSERT_FALSE(c.groupIntoOpenMacro({farOsc, farFilter}).isEmpty());

    graph_editor_paint::workCounters() = {};
    c.tick();
    c.paint();
    EXPECT_EQ(graph_editor_paint::workCounters().hullComputations, 0) << "nothing moved since the last paint";
    EXPECT_GT(graph_editor_paint::workCounters().hullsPainted, 0);

    auto* card = findComponent(c.editor, c.filter);
    card->setTopLeftPosition(card->getPosition() + juce::Point<int>(60, 0));
    graph_editor_paint::workCounters() = {};
    c.paint();
    EXPECT_GE(graph_editor_paint::workCounters().hullComputations, 1) << "the moved member's border is measured";
    EXPECT_LE(graph_editor_paint::workCounters().hullComputations, 2) << "once per border";
}
