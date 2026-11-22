// ScaleHangMacroDropTests.cpp
//
// The main-thread-stall guard for a drop out of a macro and back in on a canvas crowded with other open macros: the
// layout work the release does (layout-unit lists built, units moved, macro-card syncs), counted at N and at 4N
// neighbour macros. Counters, never clocks, so it holds on a loaded CI machine. The disabled bench at the bottom prints
// the real times; run it by hand:
//   ./Tests --gtest_also_run_disabled_tests --gtest_filter='ScaleHang.DISABLED_PrintsWhatAMacroDropCosts'

#include "../Macros/MacroContainer/MacroDragTestHelpers.h"
#include "AppUndoManager.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "StallProbe.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include <algorithm>
#include <cstdio>
#include <gtest/gtest.h>
#include <map>

namespace {

struct DropWork {
    graph_editor_paint::WorkCounters counters; // the release
    graph_editor_paint::WorkCounters drag;     // the ticks before it, the border crossing among them
    double ms = 0;
};

// A Cmd-drag of `comp` by `delta` in 12 real mouse ticks; returns what the ticks and the release each did. A synthetic
// event's position is relative to where the card is now, as a real one's is.
DropWork countedDrop(ModuleComponent& comp, juce::Point<int> delta) {
    const juce::Point<int> pressPos(comp.getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    comp.mouseDown(realMouseEvent(comp, pressPos, pressPos, kCmdClick));
    const auto start = comp.getPosition();
    constexpr int ticks = 12;
    graph_editor_paint::workCounters() = {};
    for (int i = 1; i <= ticks; ++i)
        comp.mouseDrag(realMouseEvent(comp, pressPos + delta * i / ticks - (comp.getPosition() - start), pressPos,
                                      kCmdClick, true));
    DropWork work;
    work.drag = graph_editor_paint::workCounters();
    graph_editor_paint::workCounters() = {};
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    comp.mouseUp(realMouseEvent(comp, pressPos + delta - (comp.getPosition() - start), pressPos, kCmdClick, true));
    work.ms = juce::Time::getMillisecondCounterHiRes() - t0;
    work.counters = graph_editor_paint::workCounters();
    return work;
}

// A two-member macro with `neighbours` open two-module macros packed edge to edge in rows of eight to its right, a
// cable chain running through all of them. A border that grows right on the drop pushes its whole row along.
struct CrowdedMacroCanvas {
    static constexpr int kPerRow = 8;
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID a, b;
    juce::String macroId;
    std::vector<juce::String> neighbourIds;

    explicit CrowdedMacroCanvas(int neighbours) {
        undo.setGraphEditor(&editor);
        editor.setSize(1600, 1200);
        // A VCA beside the Oscillator, so the macro and the ones beside it keep their edges on the grid.
        a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
        b = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 100, 300);
        editor.setSelectedNodes({a, b});
        macroId = editor.getMacroController().groupSelectionIntoMacro();
        editor.getMacroController().setMacroCollapsed(macroId, false);
        auto& controller = editor.getMacroController();
        const auto hull = controller.macroHullBounds(macroId);
        NodeID previous = a;
        juce::Point<int> spot(hull.getRight() + synth::LayoutUtil::kCollisionGap, hull.getY());
        int rowBottom = hull.getBottom();
        for (int i = 0; i < neighbours; ++i) {
            if (i > 0 && i % kPerRow == 0)
                spot = {hull.getRight() + synth::LayoutUtil::kCollisionGap, rowBottom + 200};
            // VCAs: 280 wide, so a row of macros stays on the grid whatever the Filter's width.
            const auto id = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 4000, 4000 + 1000 * i);
            const auto below = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 4000, 4300 + 1000 * i);
            engine.getGraph().addConnection({{previous, 0}, {id, 0}});
            engine.getGraph().addConnection({{id, 0}, {below, 0}});
            previous = below;
            editor.setSelectedNodes({id, below});
            const auto neighbour = controller.groupSelectionIntoMacro();
            EXPECT_TRUE(neighbour.isNotEmpty());
            controller.setMacroCollapsed(neighbour, false);
            neighbourIds.push_back(neighbour);
            // Built out of the way, then set edge to edge with the last one (no make-room: nothing overlaps).
            const auto built = controller.macroHullBounds(neighbour);
            controller.moveUnitBy("m:" + neighbour, spot - built.getPosition());
            // The next macro starts on the grid, as a dropped card does (a 308-wide Filter ends off it).
            const int grid = synth::LayoutUtil::kGridSize;
            spot.x = (spot.x + built.getWidth() + synth::LayoutUtil::kCollisionGap + grid - 1) / grid * grid;
            rowBottom = std::max(rowBottom, spot.y + built.getHeight());
        }
        controller.refreshAfterUnitMoves();
        editor.setSelectedNodes({});
        editor.updateComponents();
        undo.clearUndoHistory();
    }

    // Every card drawn once through its cached picture (a card not yet painted has none, so a glide would have to
    // render it), and how many times the pictures have been rendered in all.
    void paintAll() { (void)editor.createComponentSnapshot(editor.getLocalBounds()); }
    int rasterCount() {
        int total = 0;
        for (auto* card : editor.getModuleComponents())
            if (auto* cache = dynamic_cast<synth::ui::ZoomFrozenCachedImage*>(card->getCachedComponentImage()))
                total += cache->getRasterCountForTest();
        return total;
    }

    // Out of the macro to free canvas below it, then back a little right of the spot it left, so the border grows
    // into the row beside it.
    std::pair<DropWork, DropWork> dragOutAndBackIn() {
        auto* comp = findComponent(editor, a);
        const auto home = comp->getPosition();
        const auto out = countedDrop(*comp, {0, 1400});
        EXPECT_EQ(editor.getMacroController().macroForNode(a), nullptr) << "sanity: A left the macro";
        comp = findComponent(editor, a);
        const auto in = countedDrop(*comp, home + juce::Point<int>(96, 0) - comp->getPosition());
        EXPECT_NE(editor.getMacroController().macroForNode(a), nullptr) << "sanity: A rejoined the macro";
        return {out, in};
    }
};

// What one undo or redo step did: the counters across the call, and its time.
DropWork countedRestore(AppUndoManager& undo, bool redo) {
    DropWork work;
    graph_editor_paint::workCounters() = {};
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    EXPECT_TRUE(redo ? undo.redo() : undo.undo());
    work.ms = juce::Time::getMillisecondCounterHiRes() - t0;
    work.counters = graph_editor_paint::workCounters();
    return work;
}

} // namespace

// The drop that ends a Cmd-drag out of a macro or back in makes room for the macro's settled border in one pass over
// the layout units and re-syncs the macro cards at most once. It takes no cable snapshot: the crossing was applied
// mid-drag, so the drop can only flash the card, and on the Load test project each snapshot took about 18 ms. The
// snapshots the mid-drag crossing diffs ask each card for its modulation targets once, not once per cable end. The
// same however many other macros the canvas holds.
TEST(ScaleHang, ADropOutOfAMacroAndBackInMakesRoomInOnePassHoweverManyMacros) {
    struct Size {
        int unitLists, cards, cablePasses, targetReads;
    };
    std::vector<Size> sizes;
    for (const int neighbours : {8, 32}) {
        SCOPED_TRACE(neighbours);
        CrowdedMacroCanvas c(neighbours);
        const auto [out, in] = c.dragOutAndBackIn();
        for (const auto* drop : {&out, &in}) {
            EXPECT_LE(drop->counters.layoutUnitBuilds, 3);
            EXPECT_LE(drop->counters.macroCardSyncs, 1);
            EXPECT_LE(drop->counters.layoutUnitsMoved, 2 * neighbours);
            EXPECT_EQ(drop->counters.cablePasses, 0);
            EXPECT_EQ(drop->counters.componentUpdates, 0);
        }
        EXPECT_GT(out.drag.cablePasses, 0) << "sanity: the crossing diffed its cables mid-drag";
        sizes.push_back({out.counters.layoutUnitBuilds + in.counters.layoutUnitBuilds,
                         (int)c.editor.getModuleComponents().size(), out.drag.cablePasses, out.drag.modTargetReads});
    }
    EXPECT_EQ(sizes[0].unitLists, sizes[1].unitLists);
    // What the bigger canvas adds to the crossing ticks' target reads comes from their cable passes, at most one read
    // per added card each (a read per cable end would be twice that).
    ASSERT_EQ(sizes[0].cablePasses, sizes[1].cablePasses);
    EXPECT_LE(sizes[1].targetReads - sizes[0].targetReads, sizes[1].cablePasses * (sizes[1].cards - sizes[0].cards));
}

// An undo or redo of the drop is as cheap as the drop: the same one reconcile and one macro-card sync however many
// macros the canvas holds, nothing pushed again, and no card the glide moved drawn anew. The glide hides each moved
// card at alpha 0 while its picture slides and shows it again at the end; JUCE's setAlpha dropped the card's cached
// picture both times, so on the Load test project Cmd+Z re-rendered about 160 cards (about 120 ms a step, 0.5 s on a
// slow machine) when the glide began and again when it ended.
TEST(ScaleHang, AnUndoOrRedoOfAMacroDropMakesRoomInOnePassAndDrawsNoMovedCardAgainHoweverManyMacros) {
    for (const int neighbours : {8, 32}) {
        SCOPED_TRACE(neighbours);
        CrowdedMacroCanvas c(neighbours);
        auto& glide = c.editor.getCardGlideForTest();
        glide.setForceAnimateForTest(true); // not on screen: a glide only arms when forced
        (void)c.dragOutAndBackIn();
        glide.finish();
        int stepNumber = 0;
        for (const bool redo : {false, false, true, true}) {
            SCOPED_TRACE(redo ? "redo" : "undo");
            ++stepNumber;
            c.paintAll();
            const int rastersBefore = c.rasterCount();
            const int armsBefore = glide.armCount();
            const auto step = countedRestore(c.undo, redo);
            EXPECT_GT(glide.armCount(), armsBefore) << "sanity: the restore glided the moved cards";
            glide.finish();
            c.paintAll();
            EXPECT_LE(step.counters.layoutUnitBuilds, 1);
            EXPECT_LE(step.counters.macroCardSyncs, 1);
            EXPECT_EQ(step.counters.layoutUnitsMoved, 0);
            EXPECT_LE(step.counters.componentUpdates, 1);
            // The restore keeps every node, so no card is torn down and rebuilt: at most the one a step brings
            // back (a bound, not a clock; it is 0 or 1 at 8 and at 32 neighbours, the canvas holds ~125 cards).
            EXPECT_LE(step.counters.cardsBuilt, 2) << "an undo or redo rebuilt the cards of the canvas";
            // The one-shot time of the restore call itself, off screen: shown in the test's XML result.
            RecordProperty(("restoreMs_" + std::to_string(neighbours) + "_step" + std::to_string(stepNumber)).c_str(),
                           std::to_string(step.ms));
            EXPECT_LE(c.rasterCount() - rastersBefore, 1) << "a card the glide moved was drawn again";
        }
    }
}

// Making room for several growers at once gives exactly the layout their makeRoomFor calls in order give, with fewer
// passes: a grower an earlier pass already settled is not walked again.
namespace {
struct GrownLayout {
    std::vector<juce::Rectangle<int>> cards, borders; // in canvas order and macro order: the whole layout
    graph_editor_paint::WorkCounters work;
};

// The crowded canvas with three borders pushed into their neighbours without making room, then room made for
// `growers` (indices into the macros: -1 the dragged module's macro) one at a time or all at once.
GrownLayout growAndMakeRoom(const std::vector<int>& growers, bool allAtOnce) {
    CrowdedMacroCanvas c(24);
    auto& controller = c.editor.getMacroController();
    auto keyOf = [&c](int i) { return "m:" + (i < 0 ? c.macroId : c.neighbourIds[(size_t)i]); };
    controller.moveUnitBy("n:" + juce::String((juce::int64)c.b.uid), {420, 160});
    for (const int i : {0, 9}) {
        std::vector<NodeID> members;
        for (auto* card : c.editor.getModuleComponents())
            if (const auto* owner = controller.macroForNode(card->getNodeId());
                owner != nullptr && owner->id == c.neighbourIds[(size_t)i])
                members.push_back(card->getNodeId());
        for (const auto id : members)
            controller.moveUnitBy("n:" + juce::String((juce::int64)id.uid), {200, 300});
    }
    std::vector<juce::String> keys;
    for (const int i : growers)
        keys.push_back(keyOf(i));
    graph_editor_paint::workCounters() = {};
    if (allAtOnce)
        controller.makeRoomForAll(keys);
    else
        for (const auto& key : keys)
            controller.makeRoomFor(key);
    GrownLayout out;
    out.work = graph_editor_paint::workCounters();
    for (auto* card : c.editor.getModuleComponents())
        out.cards.push_back(card->getBounds());
    for (const auto& macro : c.editor.getMacros().getAll())
        out.borders.push_back(controller.macroHullBounds(macro.id));
    return out;
}
} // namespace

TEST(ScaleHang, MakingRoomForSeveralGrowersAtOnceLandsWhereOneAtATimeDoes) {
    const std::vector<int> growers{-1, 0, -1, 9, 0};
    const auto oneByOne = growAndMakeRoom(growers, false);
    const auto together = growAndMakeRoom(growers, true);
    EXPECT_GT(oneByOne.work.layoutUnitsMoved, 0) << "sanity: the growers pushed something";
    EXPECT_EQ(oneByOne.cards, together.cards);
    EXPECT_EQ(oneByOne.borders, together.borders);
    EXPECT_LT(together.work.layoutUnitBuilds, oneByOne.work.layoutUnitBuilds);
    EXPECT_LE(together.work.macroCardSyncs, 1);
}

TEST(ScaleHang, DISABLED_PrintsWhatAMacroDropCosts) {
    for (const int neighbours : {8, 32}) {
        CrowdedMacroCanvas c(neighbours);
        const auto [out, in] = c.dragOutAndBackIn();
        for (const auto& [label, drop] : {std::pair{"out", out}, std::pair{"in", in}})
            std::printf("[macro-drop] neighbours=%d %s up=%.2fms unitLists=%d unitsMoved=%d cardSyncs=%d "
                        "cablePasses=%d modTargetReads=%d reconciles=%d hulls=%d\n",
                        neighbours, label, drop.ms, drop.counters.layoutUnitBuilds, drop.counters.layoutUnitsMoved,
                        drop.counters.macroCardSyncs, drop.counters.cablePasses, drop.counters.modTargetReads,
                        drop.counters.componentUpdates, drop.counters.hullComputations);
        c.editor.getCardGlideForTest().setForceAnimateForTest(true);
        for (const bool redo : {false, false, true, true}) {
            c.paintAll();
            const int rastersBefore = c.rasterCount();
            const auto r = countedRestore(c.undo, redo);
            c.editor.getCardGlideForTest().finish();
            c.paintAll();
            std::printf("[macro-drop] neighbours=%d %s up=%.2fms unitLists=%d unitsMoved=%d cardSyncs=%d "
                        "reconciles=%d cablePasses=%d rasters=%d cardsBuilt=%d\n",
                        neighbours, redo ? "redo" : "undo", r.ms, r.counters.layoutUnitBuilds,
                        r.counters.layoutUnitsMoved, r.counters.macroCardSyncs, r.counters.componentUpdates,
                        r.counters.cablePasses, c.rasterCount() - rastersBefore, r.counters.cardsBuilt);
        }
    }
    std::fflush(stdout);
}
