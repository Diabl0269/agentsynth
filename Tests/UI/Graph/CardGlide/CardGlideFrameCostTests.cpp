// CardGlideFrameCostTests.cpp
//
// What the card glide and its delete/undo ghosts cost, as work counts rather than timings
// (docs/layout/rendering.md#per-frame-work-does-not-grow-with-the-patch): an undo pictures a card only when the
// restore tears the cards down, a picture comes from the card's own raster when it has one, and a frame repaints only
// what moves and moves the cables in the memo instead of rebuilding them. Driven through the real Delete key and
// AppUndoManager, with no VBlank, through the animator's frame seam.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include "UI/Layout/CableCurve.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>
#include <map>

namespace {

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    std::vector<NodeID> ids;

    struct FullMotion {
        FullMotion() { synth::ui::setReducedMotionForTest(false); }
        ~FullMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
    } fullMotion;

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
        ids = {addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1500, 900),
               addModuleAt(editor, engine, std::make_unique<FilterModule>(), 300, 1400),
               addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 200),
               addModuleAt(editor, engine, std::make_unique<FilterModule>(), 2000, 1800)};
        engine.getGraph().addConnection({{ids[0], 0}, {ids[1], 0}});
        engine.getGraph().addConnection({{ids[2], 0}, {ids[3], 0}});
        editor.updateComponents();
        glide().setForceAnimateForTest(true);
    }

    CardGlideAnimator& glide() { return editor.getCardGlideForTest(); }
    void paint() { (void)editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f); }
    void deleteWithKey(NodeID id) {
        editor.setSelectedNodes({id});
        ASSERT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    }
    std::map<juce::uint32, juce::Rectangle<int>> bounds() {
        std::map<juce::uint32, juce::Rectangle<int>> out;
        for (auto id : ids)
            out[id.uid] = findComponent(editor, id)->getBounds();
        return out;
    }
};

void expectSameEnds(const graph_editor_types::VisibleCable& a, const graph_editor_types::VisibleCable& b) {
    EXPECT_NEAR(a.p1.x, b.p1.x, 0.01f);
    EXPECT_NEAR(a.p1.y, b.p1.y, 0.01f);
    EXPECT_NEAR(a.p2.x, b.p2.x, 0.01f);
    EXPECT_NEAR(a.p2.y, b.p2.y, 0.01f);
}

} // namespace

TEST(CardGlideFrameCost, AParameterOnlyUndoPicturesNoCardAndArmsNothing) {
    Canvas c;
    c.paint();
    auto& graph = c.engine.getGraph();
    auto* param = graph.getNodeForId(c.ids[0])->getProcessor()->getParameters()[0];
    c.undo.captureBeforeState(graph);
    param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.1f : 0.9f);
    c.undo.pushSnapshotFromCapture(graph);
    const int snapshots = c.glide().snapshotCount();

    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.glide().snapshotCount(), snapshots) << "nothing left the canvas, so nothing was pictured";
    EXPECT_FALSE(c.glide().isLive());
}

TEST(CardGlideFrameCost, ARedoThatRemovesACardTakesItsPictureFromTheCardsRaster) {
    Canvas c;
    c.deleteWithKey(c.ids[0]);
    c.editor.finishCardGlideForTest();
    ASSERT_TRUE(c.undo.undo());
    c.editor.finishCardGlideForTest();
    c.paint(); // the restored card has a raster, as it does on screen
    const int rendered = c.glide().renderedSnapshotCount();
    const int snapshots = c.glide().snapshotCount();

    ASSERT_TRUE(c.undo.redo()); // frees the node, so every card is torn down and rebuilt
    EXPECT_EQ(findComponent(c.editor, c.ids[0]), nullptr);
    EXPECT_EQ(c.glide().exitGhostCount(), 1) << "it still shrinks away";
    EXPECT_GT(c.glide().snapshotCount(), snapshots);
    EXPECT_EQ(c.glide().renderedSnapshotCount(), rendered) << "every picture came from a raster already painted";
    c.editor.finishCardGlideForTest();
}

TEST(CardGlideFrameCost, AGhostFrameRepaintsOnlyTheGhost) {
    Canvas c;
    const auto gone = findComponent(c.editor, c.ids[2])->getBounds();
    c.deleteWithKey(c.ids[2]);
    ASSERT_EQ(c.glide().exitGhostCount(), 1);
    (void)c.editor.buildVisibleCables(); // the paint after arming rebuilt the memo

    c.glide().stepFrameForTest(0.3f);
    const auto area = c.glide().lastFrameArea();
    EXPECT_TRUE(area.contains(gone)) << "the shrinking card is repainted";
    for (auto id : {c.ids[0], c.ids[1], c.ids[3]})
        EXPECT_FALSE(area.intersects(findComponent(c.editor, id)->getBounds())) << "a card that does not move is not";
    c.editor.finishCardGlideForTest();
}

TEST(CardGlideFrameCost, AGlideFrameMovesTheCablesInTheMemoWithoutRebuildingIt) {
    Canvas c;
    const auto scrambled = c.bounds();
    c.editor.autoArrange();
    c.editor.finishCardGlideForTest();
    ASSERT_NE(c.bounds(), scrambled);
    ASSERT_TRUE(c.undo.undo());
    ASSERT_TRUE(c.glide().isLive());

    for (float t : {0.4f, 1.0f}) { // mid-glide, then landed (every cable back on its card)
        (void)c.editor.buildVisibleCables();
        const int rebuilds = c.editor.getCableRebuildCountForTest();
        c.glide().stepFrameForTest(t);
        const auto moved = c.editor.buildVisibleCables(); // a copy: the memo is rebuilt below
        EXPECT_EQ(c.editor.getCableRebuildCountForTest(), rebuilds) << "the frame kept the memo";
        ASSERT_EQ(moved.size(), 2u);
        if (t < 1.0f)
            for (const auto& cable : moved)
                EXPECT_TRUE(c.glide().lastFrameArea().contains(
                    synth::ui::cablePaintBounds(cable.p1, cable.p2).getSmallestIntegerContainer()))
                    << "a cable on a gliding card is inside the repainted area";

        c.editor.notifyModuleContentChanged(); // drops the memo
        const auto rebuilt = c.editor.buildVisibleCables();
        ASSERT_EQ(rebuilt.size(), moved.size());
        for (size_t i = 0; i < moved.size(); ++i)
            expectSameEnds(moved[i], rebuilt[i]);
    }
    c.editor.finishCardGlideForTest();
}

TEST(CardGlideFrameCost, APartialPaintSkipsTheCablesAndMacroBordersOutsideIt) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    undo.setGraphEditor(&editor);
    editor.setSize(3200, 2400);
    const auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 200, 200);
    const auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 200);
    const auto c = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 2000, 1700);
    const auto d = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 2500, 1700);
    engine.getGraph().addConnection({{a, 0}, {b, 0}});
    engine.getGraph().addConnection({{c, 0}, {d, 0}});
    editor.updateComponents();
    editor.setSelectedNodes({c, d});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    editor.finishCardGlideForTest();
    editor.finishHullGlideForTest();
    auto paint = [&editor](juce::Rectangle<int> area) {
        graph_editor_paint::workCounters() = {};
        (void)editor.createComponentSnapshot(area, true, 1.0f);
        return graph_editor_paint::workCounters();
    };

    const auto whole = paint(editor.getLocalBounds());
    ASSERT_EQ(whole.cablesPainted, 2);
    ASSERT_EQ(whole.hullsPainted, 1);

    // A small area on the a -> b cable, far from the macro and the other cable.
    const auto& first = editor.buildVisibleCables().front();
    const auto onWire = synth::ui::makeCablePath(first.p1, first.p2).getPointAlongPath(30.0f);
    auto* canvas = findComponent(editor, a)->getParentComponent();
    const auto part = paint(editor.getLocalArea(canvas, juce::Rectangle<float>(40.0f, 40.0f).withCentre(onWire))
                                .getSmallestIntegerContainer());
    EXPECT_EQ(part.cablesPainted, 1);
    EXPECT_EQ(part.hullsPainted, 0);
}
