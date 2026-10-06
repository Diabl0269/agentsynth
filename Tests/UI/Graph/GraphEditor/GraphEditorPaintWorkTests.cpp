// GraphEditorPaintWorkTests.cpp
//
// Work bounds on the canvas's per-frame passes, through the counters in GraphEditorPaintMemo.h rather than wall
// clock: a paint computes each open macro's border once, and a cable rebuild's node lookups do not grow with the
// number of cables (docs/layout/rendering.md#per-frame-work-does-not-grow-with-the-patch).

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include <functional>
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
    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }
    NodeID filter(int x, int y) { return addModuleAt(editor, engine, std::make_unique<FilterModule>(), x, y); }
};

graph_editor_paint::WorkCounters countedDuring(const std::function<void()>& work) {
    graph_editor_paint::workCounters() = {};
    work();
    return graph_editor_paint::workCounters();
}

} // namespace

TEST(GraphEditorPaintWork, APaintComputesEachOpenMacroBorderOnce) {
    Canvas c;
    constexpr int kMacros = 4;
    for (int i = 0; i < kMacros; ++i) {
        c.editor.setSelectedNodes({c.osc(200 + i * 700, 300), c.filter(500 + i * 700, 300)});
        const auto id = c.editor.getMacroController().groupSelectionIntoMacro();
        ASSERT_FALSE(id.isEmpty());
        c.editor.getMacroController().setMacroCollapsed(id, false);
    }
    c.editor.finishCardGlideForTest();
    c.editor.finishHullGlideForTest();

    const auto counts =
        countedDuring([&] { (void)c.editor.createComponentSnapshot(c.editor.getLocalBounds(), true, 1.0f); });
    EXPECT_GT(counts.hullComputations, 0) << "the paint drew the open borders";
    EXPECT_LE(counts.hullComputations, kMacros)
        << "the outline, chip, buttons and port strips of one border share one computation per paint";
}

TEST(GraphEditorPaintWork, ACableRebuildsNodeLookupsDoNotGrowWithTheNumberOfCables) {
    Canvas c;
    std::vector<NodeID> filters;
    for (int i = 0; i < 6; ++i)
        filters.push_back(c.filter(900, 200 + i * 300));
    const auto source = c.osc(200, 200);
    c.engine.getGraph().addConnection({{source, 0}, {filters[0], 0}});
    c.editor.updateComponents();

    auto rebuild = [&] {
        c.editor.notifyModuleContentChanged(); // the repaintCanvas() seam: drops the cable memo
        return (int)c.editor.buildVisibleCables().size();
    };
    int fewCables = 0;
    const auto few = countedDuring([&] { fewCables = rebuild(); });

    for (size_t i = 1; i < filters.size(); ++i)
        c.engine.getGraph().addConnection({{source, 0}, {filters[i], 0}});
    c.editor.updateComponents();
    int manyCables = 0;
    const auto many = countedDuring([&] { manyCables = rebuild(); });

    ASSERT_GT(manyCables, fewCables);
    EXPECT_EQ(many.nodeScans, few.nodeScans) << "per-cable lookups go through one map built per rebuild";
}

// A routing change re-syncs every card's mod dots on the next tick. Each card used to resolve every routing against
// the whole cable set (cards x routings x cables: the duplicate-track freeze); the tick now resolves the routings
// once, so the cable scans it costs do not grow with the number of modulated cards.
TEST(GraphEditorPaintWork, ARoutingChangeTickDoesNotScanTheCablesPerModulatedCard) {
    Canvas c;
    const auto lfo = c.osc(200, 200);
    std::vector<NodeID> filters;
    for (int i = 0; i < 6; ++i)
        filters.push_back(c.filter(900, 200 + i * 300));
    c.engine.addModRouting(lfo, 0, filters[0], 1);
    c.editor.updateComponents();
    const auto few = countedDuring([&] { c.editor.timerCallback(); });

    for (size_t i = 1; i < filters.size(); ++i)
        c.engine.addModRouting(lfo, 0, filters[i], 1);
    c.editor.updateComponents();
    const auto many = countedDuring([&] { c.editor.timerCallback(); });

    ASSERT_EQ(c.editor.getModDot().knobSourceCount(filters.back(), 1), 1) << "the tick counted the new routings";
    EXPECT_EQ(many.cableScans, few.cableScans) << "one cable index per tick, not one per card or routing";
}
