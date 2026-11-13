// CanvasEdgeScrollTests.cpp
//
// Dragging a card to the edge of the canvas pans it: the speed curve on its own, then the real gestures (a module
// card pressed and dragged through its mouse handlers, a library drag through GraphEditor's drag-and-drop
// overrides) with the pointer parked in the edge zone and the scroller ticked by hand; then what a tick costs (below).
// (docs/layout/layout.md#edge-scroll-while-dragging)

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Project/ViewDoc.h"
#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScroll.h"
#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScrollDriver.h"
#include "UI/Graph/DragPreviewGrid/DragPreviewGrid.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <cstdio>
#include <gtest/gtest.h>
#include <map>

namespace {

using synth::ui::canvasEdgePanDelta;
using synth::ui::kCanvasEdgeMaxPxPerTick;

juce::MouseEvent mouseAt(juce::Component& comp, juce::Point<int> pos, juce::Point<int> downPos, bool dragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos.toFloat(), juce::Time::getCurrentTime(),
                            1, dragged);
}

struct Canvas {
    AudioEngine engine;
    GraphEditor editor{engine, nullptr};
    juce::Point<float> pointer; // editor-local, what the scroller sees as the mouse
    double nowMs = 0.0;         // the landing preview's throttle clock: one 30 Hz frame per tick

    Canvas() {
        editor.setSize(800, 600);
        editor.getEdgeScroll().setPointerForTest([this] { return pointer; });
        editor.getDragDropController().setClockForTest([this] { return nowMs; });
    }
    NodeID osc(int x, int y) { return addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), x, y); }
    float panX() const { return editor.getViewDoc().panX; }
    float panY() const { return editor.getViewDoc().panY; }
    void tick(int times) {
        for (int i = 0; i < times; ++i) {
            nowMs += 1000.0 / 30.0;
            editor.getEdgeScroll().tickForTest();
        }
    }
};

} // namespace

TEST(CanvasEdgePanDelta, IsZeroInTheMiddleAndGrowsTowardTheEdge) {
    const juce::Rectangle<int> view(0, 0, 800, 600);
    EXPECT_EQ(canvasEdgePanDelta({400, 300}, view), juce::Point<float>());
    EXPECT_EQ(canvasEdgePanDelta({800 - synth::ui::kCanvasEdgeZonePx, 300}, view), juce::Point<float>())
        << "the zone starts exactly at its inner border";

    const float nearer = canvasEdgePanDelta({800 - 30, 300}, view).x;
    const float closer = canvasEdgePanDelta({800 - 10, 300}, view).x;
    EXPECT_LT(nearer, 0.0f) << "pointer at the right: content moves left, so the view moves right";
    EXPECT_LT(closer, nearer) << "closer to the edge is faster";
    EXPECT_FLOAT_EQ(canvasEdgePanDelta({800, 300}, view).x, -kCanvasEdgeMaxPxPerTick);
    EXPECT_FLOAT_EQ(canvasEdgePanDelta({5000, 300}, view).x, -kCanvasEdgeMaxPxPerTick) << "off the view is capped";
}

TEST(CanvasEdgePanDelta, MirrorsOnTheOtherSidesAndMovesAxesIndependently) {
    const juce::Rectangle<int> view(0, 0, 800, 600);
    EXPECT_FLOAT_EQ(canvasEdgePanDelta({0, 300}, view).x, kCanvasEdgeMaxPxPerTick);
    EXPECT_FLOAT_EQ(canvasEdgePanDelta({400, 0}, view).y, kCanvasEdgeMaxPxPerTick);
    EXPECT_FLOAT_EQ(canvasEdgePanDelta({400, 600}, view).y, -kCanvasEdgeMaxPxPerTick);
    const auto corner = canvasEdgePanDelta({800, 600}, view);
    EXPECT_FLOAT_EQ(corner.x, -kCanvasEdgeMaxPxPerTick);
    EXPECT_FLOAT_EQ(corner.y, -kCanvasEdgeMaxPxPerTick);
}

TEST(CanvasEdgePanDelta, ATinyViewKeepsADeadMiddle) {
    const juce::Rectangle<int> view(0, 0, 90, 90);
    EXPECT_EQ(canvasEdgePanDelta({45, 45}, view), juce::Point<float>());
}

// Regression test for FRO799: dragging a card to the window edge did not scroll the canvas.
TEST(CanvasEdgeScroll, ACardHeldAtTheRightEdgeScrollsTheCanvasAndKeepsTrackingUnderThePointer) {
    Canvas c;
    const auto id = c.osc(300, 200);
    auto* card = findComponent(c.editor, id);
    ASSERT_NE(card, nullptr);

    const juce::Point<int> press(card->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    card->mouseDown(mouseAt(*card, press, press, false));
    // The real drag: to the right edge of the view.
    const juce::Point<int> toEdge(795, 220);
    c.pointer = toEdge.toFloat();
    card->mouseDrag(mouseAt(*card, card->getLocalPoint(&c.editor, toEdge), press, true));
    const float panBefore = c.panX();
    const int xBefore = card->getX();
    ASSERT_GT(xBefore, 300);

    c.tick(10);

    EXPECT_LT(c.panX(), panBefore) << "the canvas scrolled to the right";
    EXPECT_GT(card->getX(), xBefore) << "the card went with it";
    EXPECT_TRUE(card->getBounds().contains(card->getParentComponent()->getLocalPoint(&c.editor, toEdge)))
        << "the card is still under the stationary pointer";
    EXPECT_EQ(card->getY(), 220 - (ModuleComponent::kHeaderHeight + 10)) << "no vertical drift";

    // Back inward: the pan stops.
    c.pointer = {400.0f, 220.0f};
    const float panStopped = c.panX();
    c.tick(5);
    EXPECT_FLOAT_EQ(c.panX(), panStopped);

    // Release ends the scroller for good.
    card->mouseUp(mouseAt(*card, press, press, true));
    EXPECT_FALSE(c.editor.getEdgeScroll().isActive());
    c.pointer = toEdge.toFloat();
    c.tick(5);
    EXPECT_FLOAT_EQ(c.panX(), panStopped);
}

TEST(CanvasEdgeScroll, TheCloserToTheEdgeTheFasterItScrolls) {
    float step[2] = {};
    for (int i = 0; i < 2; ++i) {
        Canvas c;
        const auto id = c.osc(300, 200);
        auto* card = findComponent(c.editor, id);
        const juce::Point<int> press(card->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
        card->mouseDown(mouseAt(*card, press, press, false));
        c.pointer = {i == 0 ? 770.0f : 797.0f, 220.0f};
        card->mouseDrag(mouseAt(*card, card->getLocalPoint(&c.editor, c.pointer.roundToInt()), press, true));
        const float before = c.panX();
        c.tick(1);
        step[i] = before - c.panX();
        card->mouseUp(mouseAt(*card, press, press, true));
    }
    EXPECT_GT(step[0], 0.0f);
    EXPECT_GT(step[1], step[0]);
}

TEST(CanvasEdgeScroll, TheCanvasNeverScrollsPastItsOriginWhileTheCardIsHeldAtTheLeftEdge) {
    Canvas c;
    const auto id = c.osc(100, 200);
    auto* card = findComponent(c.editor, id);
    const juce::Point<int> press(card->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    card->mouseDown(mouseAt(*card, press, press, false));
    c.pointer = {2.0f, 220.0f};
    card->mouseDrag(mouseAt(*card, card->getLocalPoint(&c.editor, c.pointer.roundToInt()), press, true));
    c.tick(10);
    EXPECT_LE(c.panX(), 0.0f) << "the left half of the canvas does not exist; nothing scrolls into it";
    EXPECT_FLOAT_EQ(c.panX(), 0.0f);
    card->mouseUp(mouseAt(*card, press, press, true));
}

// Regression test for FRO799: a card dragged in from the module library did not scroll the canvas either.
TEST(CanvasEdgeScroll, ALibraryDragHeldAtTheBottomEdgeScrollsAndTheGhostFollows) {
    Canvas c;
    c.osc(100, 100);
    struct Source : juce::Component {
    } source;
    juce::DragAndDropTarget::SourceDetails details("Oscillator", &source, juce::Point<int>(400, 590));
    c.pointer = {400.0f, 590.0f};
    c.editor.itemDragEnter(details);
    ASSERT_TRUE(c.editor.getEdgeScroll().isActive());
    const float panBefore = c.panY();
    const int ghostBefore = c.editor.getDragDropController().getDragPreviewGhost().getY();

    c.tick(8);

    EXPECT_LT(c.panY(), panBefore) << "the canvas scrolled down";
    EXPECT_GT(c.editor.getDragDropController().getDragPreviewGhost().getY(), ghostBefore)
        << "the landing ghost stayed under the pointer, so it moved down in canvas space";

    c.editor.itemDragExit(details);
    EXPECT_FALSE(c.editor.getEdgeScroll().isActive());
    const float stopped = c.panY();
    c.tick(3);
    EXPECT_FLOAT_EQ(c.panY(), stopped);
}

// ---- What a tick costs ----
//
// The guard: for every kind of drag the scroll drives (a library drag, a module card, a folded macro card), held still
// at the edge, the full landing preview is recomputed a bounded number of times, and the drop is the same as with the
// throttle off. The disabled bench at the bottom times the ticks on a real project; run it by hand:
//   AGENTSYNTH_BENCH_PROJECT=<copy of a project.json> ./Tests --gtest_also_run_disabled_tests \
//       --gtest_filter='CanvasEdgeScrollCost.DISABLED_*'

namespace {

struct LibrarySource : juce::Component {};

enum class DragKind { Library, ModuleCard, MacroCard };
enum class Clock { Frozen, ThirtyHz, Unthrottled };

struct HeldDrag {
    int fullPreviews = 0;
    std::map<juce::uint32, juce::Point<int>> positions; // every card after the drop, by node id (0: the folded card)
    std::map<juce::uint32, int> owners;                 // each node's macro, as its member count (0: loose)
};

// A crowded canvas (a 5 x 4 grid of cards and a folded macro), one drag of `kind` taken to the bottom edge of the view
// and held there for `ticks` scroller ticks, then released. `clock` drives the preview throttle: frozen, advancing one
// 30 Hz frame per tick, or jumping past the interval on every read (no throttling).
HeldDrag holdAtTheBottomEdge(DragKind kind, Clock clock, int ticks) {
    AudioEngine engine;
    GraphEditor editor{engine, nullptr};
    editor.setSize(800, 600);
    std::vector<NodeID> cards;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 5; ++col)
            cards.push_back(
                addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 40 + col * 330, 40 + row * 640));
    const auto f1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1900, 40);
    const auto f2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 2300, 40);
    editor.setSelectedNodes({f1, f2});
    const auto folded = editor.getMacroController().groupSelectionIntoMacro();
    editor.getMacroController().setMacroCollapsed(folded, true);
    editor.clearSelection();
    editor.updateComponents();
    editor.finishCardGlideForTest();

    double nowMs = 0.0;
    editor.getDragDropController().setClockForTest([&] {
        if (clock == Clock::Unthrottled)
            nowMs += 1000.0;
        return nowMs;
    });
    const juce::Point<float> edge{400.0f, 596.0f};
    juce::Point<float> pointer = edge;
    editor.getEdgeScroll().setPointerForTest([&] { return pointer; });
    const auto tickAll = [&] {
        for (int i = 0; i < ticks; ++i) {
            if (clock == Clock::ThirtyHz)
                nowMs += 1000.0 / 30.0;
            editor.getEdgeScroll().tickForTest();
        }
    };

    if (kind == DragKind::Library) {
        LibrarySource source;
        juce::DragAndDropTarget::SourceDetails details("Oscillator", &source, edge.roundToInt());
        editor.itemDragEnter(details);
        EXPECT_TRUE(editor.getEdgeScroll().isActive());
        tickAll();
        editor.itemDropped(details);
    } else {
        juce::Component* card = nullptr;
        if (kind == DragKind::ModuleCard)
            card = findComponent(editor, cards[6]);
        else
            card = editor.getMacroController().getMacroCardForTest(folded);
        EXPECT_NE(card, nullptr);
        if (card == nullptr)
            return {};
        const juce::Point<int> press(card->getWidth() / 2, 20);
        card->mouseDown(mouseAt(*card, press, press, false));
        card->mouseDrag(mouseAt(*card, card->getLocalPoint(&editor, edge.roundToInt()), press, true));
        EXPECT_TRUE(editor.getEdgeScroll().isActive());
        tickAll();
        card->mouseUp(mouseAt(*card, card->getLocalPoint(&editor, edge.roundToInt()), press, true));
    }
    editor.finishCardGlideForTest();

    HeldDrag out;
    out.fullPreviews = editor.getDragDropController().fullPreviewCount();
    // Uuids and macro ids are fresh in every run, so the drop is compared by node id and macro size.
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr) {
            out.positions[c->getNodeId().uid] = c->getPosition();
            const auto* owner = editor.getMacros().findByMember(uuidOf(engine, c->getNodeId()));
            out.owners[c->getNodeId().uid] = owner != nullptr ? (int)owner->members.size() : 0;
        }
    if (auto* card = editor.getMacroController().getMacroCardForTest(folded))
        out.positions[0] = card->getPosition();
    return out;
}

const char* nameOf(DragKind kind) {
    return kind == DragKind::Library ? "library drag" : kind == DragKind::ModuleCard ? "module card" : "macro card";
}

} // namespace

// The pointer parked at the edge re-aims the drag every tick; on a big patch one full landing preview can cost tens of
// ms at a crowded aim, so a tick refreshes it at most every kScrollPreviewIntervalMs, and the drop always uses one for
// the final aim.
TEST(CanvasEdgeScrollCost, EveryDragKindHeldAtTheEdgeRecomputesThePreviewABoundedNumberOfTimes) {
    constexpr int kTicks = 30; // one second at 30 Hz
    const int interval = (int)GraphDragDropController::kScrollPreviewIntervalMs;
    for (const auto kind : {DragKind::Library, DragKind::ModuleCard, DragKind::MacroCard}) {
        SCOPED_TRACE(nameOf(kind));
        const auto frozen = holdAtTheBottomEdge(kind, Clock::Frozen, kTicks);
        const auto paced = holdAtTheBottomEdge(kind, Clock::ThirtyHz, kTicks);
        const auto unthrottled = holdAtTheBottomEdge(kind, Clock::Unthrottled, kTicks);
        // The press or enter, the drop's flush, and at most one per interval of ticks.
        EXPECT_LE(frozen.fullPreviews, 3);
        EXPECT_LE(paced.fullPreviews, 3 + 1000 / interval);
        if (kind != DragKind::MacroCard) // a folded macro card draws no landing ghost
            EXPECT_GE(unthrottled.fullPreviews, kTicks) << "without the throttle every tick recomputes";
        EXPECT_EQ(frozen.positions, unthrottled.positions) << "the drop lands where the unthrottled drop lands";
        EXPECT_EQ(paced.positions, unthrottled.positions);
        EXPECT_EQ(frozen.owners, unthrottled.owners);
        EXPECT_EQ(paced.owners, unthrottled.owners);
    }
}

// A tick that cannot pan (the view already at the end of the content) brings a skipped preview up to date, so the
// ghost never stays behind the pointer while the drag waits there.
TEST(CanvasEdgeScrollCost, ATickThatCannotPanBringsASkippedPreviewUpToDate) {
    AudioEngine engine;
    GraphEditor editor{engine, nullptr};
    editor.setSize(800, 600);
    addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 40, 40);
    editor.updateComponents();
    double nowMs = 0.0;
    editor.getDragDropController().setClockForTest([&] { return nowMs; });
    juce::Point<float> pointer{400.0f, 596.0f};
    editor.getEdgeScroll().setPointerForTest([&] { return pointer; });
    LibrarySource source;
    juce::DragAndDropTarget::SourceDetails details("Oscillator", &source, pointer.roundToInt());
    editor.itemDragEnter(details);
    auto& dd = editor.getDragDropController();
    const int afterEnter = dd.fullPreviewCount();
    editor.getEdgeScroll().tickForTest(); // pans; the clock has not moved, so the preview waits
    EXPECT_EQ(dd.fullPreviewCount(), afterEnter);
    const auto stale = dd.getDragPreviewGhost();
    pointer = {400.0f, 300.0f}; // back in the middle: the next tick does not pan
    editor.getEdgeScroll().tickForTest();
    EXPECT_EQ(dd.fullPreviewCount(), afterEnter + 1);
    EXPECT_NE(dd.getDragPreviewGhost(), stale) << "the ghost caught up with the scrolled canvas";
    editor.itemDragExit(details);
}

// The grid a drag shows is one tiled fill, so a zoomed-out view of a big patch costs no more than a zoomed-in one; the
// dots still sit on every fifth snap step, at any zoom.
TEST(CanvasEdgeScrollCost, TheDragGridPutsADotOnEveryMajorGridPointAtAnyZoom) {
    constexpr int step = synth::LayoutUtil::kGridSize * 5;
    for (const float zoom : {0.5f, 1.0f, 2.0f}) {
        SCOPED_TRACE(zoom);
        juce::Image image(juce::Image::ARGB, 400, 400, true);
        {
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(zoom)); // canvas -> view, as the content component's transform
            synth::ui::paintDragPreviewGrid(g, juce::Colours::white);
        }
        const auto at = [&](float canvasX, float canvasY) {
            return image.getPixelAt(juce::roundToInt(canvasX * zoom), juce::roundToInt(canvasY * zoom)).getAlpha();
        };
        EXPECT_GT(at(step, step), 0) << "a dot on the grid point";
        EXPECT_GT(at(3 * step, 2 * step), 0);
        EXPECT_EQ(at(step * 1.5f, step * 1.5f), 0) << "nothing between grid points";
        EXPECT_EQ(at(step * 1.5f, step), 0);
    }
}

TEST(CanvasEdgeScrollCost, DISABLED_PrintsWhatATickCostsOnARealProject) {
    const auto* path = std::getenv("AGENTSYNTH_BENCH_PROJECT");
    if (path == nullptr)
        GTEST_SKIP() << "set AGENTSYNTH_BENCH_PROJECT to a copy of a project.json";
    const auto json = juce::JSON::parse(juce::File(path).loadFileAsString());
    AudioEngine engine;
    GraphEditor editor{engine, nullptr};
    editor.setSize(1600, 1000);
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, engine.getGraph(), true, true));
    EXPECT_TRUE(editor.getMacros().fromVar(json["macros"]));
    editor.updateComponents();
    juce::Point<float> pointer{800.0f, 995.0f};
    editor.getEdgeScroll().setPointerForTest([&] { return pointer; });
    juce::Image image(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
    const auto now = [] { return juce::Time::getMillisecondCounterHiRes(); };
    const auto report = [&](const char* what, int ticks, double tickMs, double paintMs) {
        std::printf("[bench] %-22s %d ticks: %.2f ms/tick (pan + move), paint %.2f ms/frame\n", what, ticks,
                    tickMs / ticks, paintMs / ticks);
    };
    constexpr int kTicks = 60;
    std::printf("[bench] %d nodes, %d macros\n", engine.getGraph().getNumNodes(),
                (int)editor.getMacros().getAll().size());

    {
        juce::Graphics g(image);
        const auto t0 = now();
        for (int i = 0; i < 10; ++i)
            editor.paintEntireComponent(g, false);
        std::printf("[bench] %-22s paint %.2f ms/frame\n", "at rest", (now() - t0) / 10);
    }
    {
        // Panned between frames like an edge scroll, with no drag: what the pan alone costs to paint.
        juce::Graphics g(image);
        auto view = editor.getViewDoc();
        double ms = 0;
        for (int i = 0; i < 10; ++i) {
            view.panY -= 22.0f;
            editor.applyViewDoc(view);
            const auto t0 = now();
            editor.paintEntireComponent(g, false);
            ms += now() - t0;
        }
        std::printf("[bench] %-22s paint %.2f ms/frame\n", "panning, no drag", ms / 10);
        view.panY += 220.0f;
        editor.applyViewDoc(view);
    }
    {
        // A drag preview open but the pointer still: the drag-only layers (grid, ghost) without the pan.
        LibrarySource source;
        juce::DragAndDropTarget::SourceDetails details("Oscillator", &source, pointer.roundToInt());
        editor.itemDragEnter(details);
        editor.getEdgeScroll().end();
        juce::Graphics g(image);
        const auto t0 = now();
        for (int i = 0; i < 10; ++i)
            editor.paintEntireComponent(g, false);
        std::printf("[bench] %-22s paint %.2f ms/frame\n", "drag preview, still", (now() - t0) / 10);
        editor.itemDragExit(details);
    }
    {
        // Every aim over the whole content: the worst one is where a drag to the edge stalls.
        LibrarySource source;
        juce::DragAndDropTarget::SourceDetails details("Oscillator", &source, {800, 500});
        editor.itemDragEnter(details);
        double worst = 0, total = 0;
        int count = 0;
        juce::Point<int> worstAt;
        juce::Rectangle<int> extent;
        juce::Component* canvas = nullptr;
        for (auto* c : editor.getModuleComponents())
            if (c != nullptr) {
                extent = extent.getUnion(c->getBounds());
                canvas = c->getParentComponent();
            }
        ASSERT_NE(canvas, nullptr);
        for (int y = extent.getY(); y < extent.getBottom(); y += 150)
            for (int x = extent.getX(); x < extent.getRight(); x += 150) {
                details.localPosition = editor.getLocalPoint(canvas, juce::Point<int>(x, y));
                const auto t0 = now();
                editor.getDragDropController().itemDragMove(details);
                const auto ms = now() - t0;
                total += ms;
                ++count;
                if (ms > worst) {
                    worst = ms;
                    worstAt = {x, y};
                }
            }
        editor.itemDragExit(details);
        std::printf(
            "[bench] itemDragMove over %d aims: mean %.2f ms, worst %.2f ms at canvas (%d, %d), cards span %dx%d\n",
            count, total / count, worst, worstAt.x, worstAt.y, extent.getWidth(), extent.getHeight());
    }
    {
        LibrarySource source;
        juce::DragAndDropTarget::SourceDetails details("Oscillator", &source, pointer.roundToInt());
        editor.itemDragEnter(details);
        double tickMs = 0, paintMs = 0, moveMs = 0;
        for (int i = 0; i < kTicks; ++i) {
            auto t0 = now();
            editor.getEdgeScroll().tickForTest();
            tickMs += now() - t0;
            t0 = now();
            editor.getDragDropController().itemDragMove(details);
            moveMs += now() - t0;
            juce::Graphics g(image);
            t0 = now();
            editor.paintEntireComponent(g, false);
            paintMs += now() - t0;
        }
        editor.itemDragExit(details);
        report("library drag", kTicks, tickMs, paintMs);
        std::printf("[bench] %-22s itemDragMove alone %.2f ms\n", "library drag", moveMs / kTicks);
    }
    {
        ModuleComponent* card = nullptr;
        for (auto* c : editor.getModuleComponents())
            if (c != nullptr && c->isVisible() && card == nullptr)
                card = c;
        ASSERT_NE(card, nullptr);
        const juce::Point<int> press(card->getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
        card->mouseDown(mouseAt(*card, press, press, false));
        card->mouseDrag(mouseAt(*card, card->getLocalPoint(&editor, pointer.roundToInt()), press, true));
        double tickMs = 0, paintMs = 0;
        for (int i = 0; i < kTicks; ++i) {
            auto t0 = now();
            editor.getEdgeScroll().tickForTest();
            tickMs += now() - t0;
            juce::Graphics g(image);
            t0 = now();
            editor.paintEntireComponent(g, false);
            paintMs += now() - t0;
        }
        card->mouseUp(mouseAt(*card, press, press, true));
        report("module card drag", kTicks, tickMs, paintMs);
    }
}
