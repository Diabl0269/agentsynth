// GraphEditor viewport tests: Minimap (issue #159) visibility/model and the zoom-perf cable
// memoization + zoom-gesture raster freeze.
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "GraphEditorTestHelpers.h"

#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"

// ============================================================================
// Minimap (issue #159)
// ============================================================================

namespace {

// Hand-built MouseEvent, same pattern as MinimapComponentTests.cpp — no OS mouse source exists
// headlessly, but MouseInputSource is copyable and Desktop always exposes one.
juce::MouseEvent makeGraphEditorMouseEvent(juce::Component& comp, juce::Point<float> position) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys(), 0.0f,
                            0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), position,
                            juce::Time::getCurrentTime(), 1, false);
}

// Maps a GraphEditor-local screen point to the canvas point currently under it, derived purely
// from getVisibleCanvasRect() (no access to the private pan/zoom state needed).
juce::Point<float> screenToCanvas(const GraphEditor& editor, juce::Point<float> screenPt) {
    const auto rect = editor.getVisibleCanvasRect();
    const auto w = static_cast<float>(editor.getWidth());
    const auto h = static_cast<float>(editor.getHeight());
    return {rect.getX() + (screenPt.x / w) * rect.getWidth(), rect.getY() + (screenPt.y / h) * rect.getHeight()};
}

} // namespace

// toggleMinimapVisibility() flips the reported preference each call.
TEST_F(GraphEditorTest, ToggleMinimapVisibilityFlipsState) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    ASSERT_TRUE(editor.isMinimapVisible());
    editor.toggleMinimapVisibility();
    EXPECT_FALSE(editor.isMinimapVisible());
    editor.toggleMinimapVisibility();
    EXPECT_TRUE(editor.isMinimapVisible());
}

// setMinimapVisible(false) actually hides the child component, not just the preference flag.
TEST_F(GraphEditorTest, SetMinimapVisibleFalseHidesChildComponent) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    ASSERT_TRUE(editor.getMinimap().isVisible());
    editor.setMinimapVisible(false);
    EXPECT_FALSE(editor.isMinimapVisible());
    EXPECT_FALSE(editor.getMinimap().isVisible());
}

// Below the 480x360 auto-hide threshold the minimap child must not be visible even though the
// user preference is untouched; growing back above the threshold restores it.
TEST_F(GraphEditorTest, MinimapAutoHidesBelowThresholdAndPreferenceSurvives) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    ASSERT_TRUE(editor.getMinimap().isVisible());

    editor.setSize(400, 300); // below kMinEditorW/H
    EXPECT_TRUE(editor.isMinimapVisible()) << "preference must survive an auto-hide";
    EXPECT_FALSE(editor.getMinimap().isVisible());

    editor.setSize(800, 600); // back above threshold
    EXPECT_TRUE(editor.isMinimapVisible());
    EXPECT_TRUE(editor.getMinimap().isVisible());
}

// getVisibleCanvasRect() at identity zoom/pan equals the editor's own local bounds.
TEST_F(GraphEditorTest, VisibleCanvasRectAtIdentityEqualsLocalBounds) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    const auto rect = editor.getVisibleCanvasRect();
    EXPECT_NEAR(rect.getX(), 0.0f, 0.01f);
    EXPECT_NEAR(rect.getY(), 0.0f, 0.01f);
    EXPECT_NEAR(rect.getWidth(), 800.0f, 0.01f);
    EXPECT_NEAR(rect.getHeight(), 600.0f, 0.01f);
}

// centreViewOn(p) must put p at the centre of the returned visible-canvas rect.
TEST_F(GraphEditorTest, CentreViewOnMovesViewportCentreToRequestedPoint) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    const juce::Point<float> target(1000.0f, -200.0f);
    editor.centreViewOn(target);

    const auto rect = editor.getVisibleCanvasRect();
    EXPECT_NEAR(rect.getCentreX(), target.x, 0.5f);
    EXPECT_NEAR(rect.getCentreY(), target.y, 0.5f);
}

// zoomAroundCentre's sign matches wheel-zoom direction: positive narrows the visible rect (zoom
// in), negative widens it (zoom out).
TEST_F(GraphEditorTest, ZoomAroundCentreMatchesWheelZoomDirection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    const auto widthBefore = editor.getVisibleCanvasRect().getWidth();
    editor.zoomAroundCentre(1.0f);
    EXPECT_LT(editor.getVisibleCanvasRect().getWidth(), widthBefore);

    const auto widthBeforeOut = editor.getVisibleCanvasRect().getWidth();
    editor.zoomAroundCentre(-1.0f);
    EXPECT_GT(editor.getVisibleCanvasRect().getWidth(), widthBeforeOut);
}

// zoomAroundCentre keeps the canvas point at the viewport centre fixed while zooming.
TEST_F(GraphEditorTest, ZoomAroundCentreKeepsCanvasCentrePointFixed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    const auto canvasCentreBefore = editor.getVisibleCanvasRect().getCentre();
    editor.zoomAroundCentre(1.0f);
    const auto canvasCentreAfter = editor.getVisibleCanvasRect().getCentre();

    EXPECT_NEAR(canvasCentreAfter.x, canvasCentreBefore.x, 0.5f);
    EXPECT_NEAR(canvasCentreAfter.y, canvasCentreBefore.y, 0.5f);
}

// Zoom stays clamped to [0.1, 2.0] no matter how many times it's driven in one direction.
TEST_F(GraphEditorTest, ZoomAroundCentreStaysClampedUnderRepeatedCalls) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    for (int i = 0; i < 200; ++i)
        editor.zoomAroundCentre(10.0f);
    EXPECT_NEAR(editor.getVisibleCanvasRect().getWidth(), 800.0f / 2.0f, 1.0f) << "zoom must clamp at 2.0";

    for (int i = 0; i < 200; ++i)
        editor.zoomAroundCentre(-10.0f);
    EXPECT_NEAR(editor.getVisibleCanvasRect().getWidth(), 800.0f / 0.1f, 1.0f) << "zoom must clamp at 0.1";
}

// Regression guard for the applyZoomAt extraction (shared by mouseWheelMove and
// zoomAroundCentre): a wheel event at an arbitrary screen position must still keep the canvas
// point under the cursor fixed, and must still actually change the zoom.
TEST_F(GraphEditorTest, WheelZoomKeepsCanvasPointUnderCursorFixed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    // Start from a non-trivial pan so this isn't only exercising the identity case.
    editor.centreViewOn({300.0f, 250.0f});

    const juce::Point<float> cursor(150.0f, 400.0f);
    const auto canvasBefore = screenToCanvas(editor, cursor);
    const auto widthBefore = editor.getVisibleCanvasRect().getWidth();

    juce::MouseWheelDetails wheel{}; // value-init: the struct has no default member initialisers
    wheel.deltaY = 1.5f;
    editor.mouseWheelMove(makeGraphEditorMouseEvent(editor, cursor), wheel);

    const auto canvasAfter = screenToCanvas(editor, cursor);
    EXPECT_NEAR(canvasAfter.x, canvasBefore.x, 0.5f);
    EXPECT_NEAR(canvasAfter.y, canvasBefore.y, 0.5f);
    EXPECT_LT(editor.getVisibleCanvasRect().getWidth(), widthBefore) << "the wheel event must still have zoomed";
}

// buildMinimapModel() returns one node per rendered ModuleComponent, and a viewport equal to
// getVisibleCanvasRect().
TEST_F(GraphEditorTest, BuildMinimapModelReturnsOneNodePerModuleAndMatchingViewport) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    oscNode->properties.set("x", 50);
    oscNode->properties.set("y", 50);
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    filterNode->properties.set("x", 400);
    filterNode->properties.set("y", 300);
    editor.updateComponents();

    const auto model = editor.buildMinimapModel();
    EXPECT_EQ(model.nodes.size(), 2u);
    EXPECT_TRUE(model.viewport == editor.getVisibleCanvasRect());
}

// --- Zoom-perf: cable memoization + zoom-gesture raster freeze --------------

namespace {
struct OscFilterVcaChain {
    ModuleComponent* osc = nullptr;
    ModuleComponent* filter = nullptr;
    ModuleComponent* vca = nullptr;
};

// Oscillator -> Filter and Oscillator -> VCA: 3 modules, 2 plain audio cables. Both cables leave
// the same source so no channel/poly mapping quirks are in play — just geometry to memoize.
OscFilterVcaChain buildOscFilterVcaChain(AudioEngine& engine, GraphEditor& editor) {
    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}});
    graph.addConnection({{oscNode->nodeID, 0}, {vcaNode->nodeID, 0}});
    editor.updateComponents();

    OscFilterVcaChain result;
    if (auto* content = editor.getChildComponent(0)) {
        for (auto* child : content->getChildren()) {
            if (auto* mc = dynamic_cast<ModuleComponent*>(child)) {
                if (mc->getModule() == oscNode->getProcessor())
                    result.osc = mc;
                else if (mc->getModule() == filterNode->getProcessor())
                    result.filter = mc;
                else if (mc->getModule() == vcaNode->getProcessor())
                    result.vca = mc;
            }
        }
    }
    // Explicit bounds (as the Dual I/O cable tests do above): a card's default constructed size
    // is enough to have real ports, but a known, non-overlapping layout keeps geometry legible.
    if (result.osc != nullptr)
        result.osc->setBounds(0, 0, 200, 200);
    if (result.filter != nullptr)
        result.filter->setBounds(300, 0, 200, 200);
    if (result.vca != nullptr)
        result.vca->setBounds(300, 300, 200, 200);
    return result;
}
} // namespace

// Cable geometry is canvas-space (zoom/pan-invariant): a zoom gesture must reuse the memoized
// list rather than rebuilding it, and the geometry it returns must not move at all.
TEST_F(GraphEditorTest, ZoomDoesNotRebuildTheCableList) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    auto chain = buildOscFilterVcaChain(engine, editor);
    ASSERT_NE(chain.osc, nullptr);
    ASSERT_NE(chain.filter, nullptr);
    ASSERT_NE(chain.vca, nullptr);

    const std::vector<GraphEditor::VisibleCable> before = editor.buildVisibleCables();
    ASSERT_EQ(before.size(), 2u);
    const int rebuildBefore = editor.getCableRebuildCountForTest();

    for (int i = 0; i < 20; ++i) {
        editor.zoomAroundCentre(0.05f);
        const auto& after = editor.buildVisibleCables();
        ASSERT_EQ(after.size(), before.size());
        for (size_t j = 0; j < after.size(); ++j) {
            EXPECT_FLOAT_EQ(after[j].p1.x, before[j].p1.x);
            EXPECT_FLOAT_EQ(after[j].p1.y, before[j].p1.y);
            EXPECT_FLOAT_EQ(after[j].p2.x, before[j].p2.x);
            EXPECT_FLOAT_EQ(after[j].p2.y, before[j].p2.y);
        }
    }
    EXPECT_EQ(editor.getCableRebuildCountForTest(), rebuildBefore)
        << "zoom must never touch the cable memo — zoom cannot move a cable";
}

// The 30 Hz tick is the ONLY thing that must keep the memo fresh absent an explicit graph edit
// (it is what re-reads activity/bypass values onto existing cables).
TEST_F(GraphEditorTest, TheThirtyHzTickInvalidatesTheCableCache) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    buildOscFilterVcaChain(engine, editor);

    editor.buildVisibleCables();
    const int before = editor.getCableRebuildCountForTest();
    editor.timerCallback();
    editor.buildVisibleCables();
    EXPECT_EQ(editor.getCableRebuildCountForTest(), before + 1);
}

// paint() and hit-testing must read the literal same list — the strengthened §14 invariant.
TEST_F(GraphEditorTest, PaintAndHitTestShareOneBuild) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    buildOscFilterVcaChain(engine, editor);

    const auto& cables = editor.buildVisibleCables();
    ASSERT_FALSE(cables.empty());
    const auto first = cables.front();
    const int rebuildAfterFirstBuild = editor.getCableRebuildCountForTest();

    // A point that lies exactly ON the drawn bezier, regardless of the card layout above.
    const auto path = GraphEditor::buildCablePath(first.p1, first.p2);
    const auto midpoint = path.getPointAlongPath(path.getLength() * 0.5f);

    const auto hit = editor.getCableAt(midpoint);
    ASSERT_TRUE(hit.has_value());
    EXPECT_TRUE(hit->id == first.id);
    EXPECT_EQ(editor.getCableRebuildCountForTest(), rebuildAfterFirstBuild)
        << "getCableAt must reuse the memoized list, not rebuild it";
}

// A graph edit (disconnect, or a node add via updateComponents()) invalidates the memo
// immediately — the NEXT build reflects it, but nothing rebuilds until asked.
TEST_F(GraphEditorTest, AGraphEditInvalidatesImmediately) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    buildOscFilterVcaChain(engine, editor);

    const std::vector<GraphEditor::VisibleCable> before = editor.buildVisibleCables();
    ASSERT_EQ(before.size(), 2u);
    const auto toRemove = before.front();
    const int rebuildBeforeDisconnect = editor.getCableRebuildCountForTest();

    editor.disconnectCable(toRemove);
    // Invalidated, not yet rebuilt: repaintCanvas() only drops the memo.
    EXPECT_EQ(editor.getCableRebuildCountForTest(), rebuildBeforeDisconnect);

    const auto& afterDisconnect = editor.buildVisibleCables();
    EXPECT_EQ(editor.getCableRebuildCountForTest(), rebuildBeforeDisconnect + 1);
    EXPECT_EQ(afterDisconnect.size(), 1u);
    for (const auto& c : afterDisconnect)
        EXPECT_FALSE(c.id == toRemove.id);

    // Same shape for updateComponents(): a node appearing invalidates immediately too.
    const int rebuildBeforeAdd = editor.getCableRebuildCountForTest();
    engine.getGraph().addNode(std::make_unique<VCAModule>());
    editor.updateComponents();
    EXPECT_EQ(editor.getCableRebuildCountForTest(), rebuildBeforeAdd);
    editor.buildVisibleCables();
    EXPECT_EQ(editor.getCableRebuildCountForTest(), rebuildBeforeAdd + 1);
}

// A zoom gesture freezes every card's raster scale; settling (here, forced via the test seam
// since the VBlank driver never ticks headless) thaws every card again.
TEST_F(GraphEditorTest, ZoomFreezesEveryCardThenSettleThaws) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    buildOscFilterVcaChain(engine, editor);

    EXPECT_FALSE(editor.isZoomGestureActive());
    editor.zoomAroundCentre(0.1f);
    EXPECT_TRUE(editor.isZoomGestureActive());
    ASSERT_FALSE(editor.getModuleComponents().isEmpty());
    for (auto* mc : editor.getModuleComponents())
        EXPECT_TRUE(mc->isRasterFrozen());

    editor.settleZoomNowForTest();
    EXPECT_FALSE(editor.isZoomGestureActive());
    for (auto* mc : editor.getModuleComponents())
        EXPECT_FALSE(mc->isRasterFrozen());
}

// A card created mid-gesture (paste/duplicate/drop while zooming) must join the freeze, or it
// rasterizes once at the pre-gesture scale and again at thaw instead of exactly once overall.
TEST_F(GraphEditorTest, ACardCreatedMidGestureJoinsTheFreeze) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    auto chain = buildOscFilterVcaChain(engine, editor);

    editor.zoomAroundCentre(0.1f);
    ASSERT_TRUE(editor.isZoomGestureActive());

    auto newNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    editor.updateComponents();

    ModuleComponent* newComp = nullptr;
    for (auto* mc : editor.getModuleComponents())
        if (mc->getModule() == newNode->getProcessor())
            newComp = mc;
    ASSERT_NE(newComp, nullptr);
    EXPECT_TRUE(newComp->isRasterFrozen());
}

// A wheel tick clamped at the [0.1, 2.0] ceiling/floor must not start (or refresh) a gesture, or
// cards at the extremes of the zoom range would be left soft forever (oldZoom == zoomLevel guard).
TEST_F(GraphEditorTest, AClampedZoomTickDoesNotStartAGesture) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    for (int i = 0; i < 200; ++i)
        editor.zoomAroundCentre(10.0f); // drive to the 2.0 ceiling
    editor.settleZoomNowForTest();
    ASSERT_FALSE(editor.isZoomGestureActive());

    editor.zoomAroundCentre(0.1f); // already clamped: must be a no-op on zoomLevel
    EXPECT_FALSE(editor.isZoomGestureActive());
}
