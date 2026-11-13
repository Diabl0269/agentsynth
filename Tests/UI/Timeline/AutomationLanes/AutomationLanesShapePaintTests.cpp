// AutomationLanesShapePaintTests.cpp -- what a lane draws around the Draw shapes: the shape previewed
// inside the box mid-drag (read back from rendered pixels), and crowded point handles left out so a dense
// stamp reads as a curve, coming back on zoom-in and still grabbable there.

#include "AutomationLanesTestFixture.h"
#include "UI/Layout/PointEditorDuplicate.h"
#include "UI/Timeline/AutomationLanes/AutomationHandleDensity.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/LaneShapeGenerator.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::AutomationLaneEditor;
using synth::ui::DrawShape;
using synth::ui::EditTool;
using synth::ui::TimelineViewState;

namespace {

// The gesture's accent without a themed LookAndFeel (the fixture panel has none).
const juce::Colour kAccent(0xff00D1FF);

struct PaintLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;

    PaintLane() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        lane = addLane(bass, "cutoff");
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
    }
    float xAt(double beat) { return (float)panel.getViewState().beatToX(beat); }
    float yAt(double value) { return (float)editor->valueToY(value); }

    juce::Image render() {
        juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        editor->paintEntireComponent(g, true);
        return image;
    }
};

bool isAccent(juce::Colour c) {
    return std::abs((int)c.getRed() - (int)kAccent.getRed()) <= 12 &&
           std::abs((int)c.getGreen() - (int)kAccent.getGreen()) <= 12 &&
           std::abs((int)c.getBlue() - (int)kAccent.getBlue()) <= 12 && c.getAlpha() > 200;
}

} // namespace

TEST(AutomationLanesShapePaintTest, TheShapeIsPreviewedInsideTheBoxWhileDragging) {
    PaintLane f;
    ASSERT_NE(f.editor, nullptr);
    f.panel.getViewState().snap = TimelineViewState::Snap::Sixteenth;
    f.panel.setActiveTool(EditTool::Draw);
    f.panel.setDrawShape(DrawShape::Sine);

    const juce::Point<float> from{f.xAt(0.0), f.yAt(100.0)}, to{f.xAt(8.0), f.yAt(0.0)};
    f.editor->mouseDown(makeClickEvent(*f.editor, from, leftButton()));
    f.editor->mouseDrag(makeDragEvent(*f.editor, to, from, leftButton()));
    ASSERT_TRUE(f.editor->getShapeGesture().getChipText().isNotEmpty());

    const auto image = f.render();
    // Right of the chip (top-left of the box) and inside the dashed outline: only the preview stroke is accent.
    const int x0 = (int)f.xAt(4.0), x1 = (int)to.x - 4;
    int accentPixels = 0;
    for (int x = x0; x < x1; ++x)
        for (int y = 2; y < image.getHeight() - 2; ++y)
            if (isAccent(image.getPixelAt(x, y)))
                ++accentPixels;
    EXPECT_GT(accentPixels, (x1 - x0) / 2) << "the sine is drawn inside the box mid-drag";
    f.editor->mouseUp(makeDragEvent(*f.editor, to, from, leftButton()));
}

// Regression test for FRO1195: a crowded pair used to lose both dots, and a greedy mask lost one of them.
TEST(AutomationLanesShapePaintTest, EveryCrowdedClusterKeepsItsFirstAndLastHandle) {
    using synth::ui::visibleHandleMask;
    EXPECT_EQ(visibleHandleMask({{0.0f, 10.0f}, {40.0f, 10.0f}, {45.0f, 10.0f}, {90.0f, 10.0f}}, 20.0f),
              (std::vector<bool>{true, true, true, true}))
        << "a pair 5 px apart shows both dots";
    EXPECT_EQ(visibleHandleMask({{0.0f, 0.0f}, {5.0f, 30.0f}, {10.0f, 0.0f}}, 20.0f),
              (std::vector<bool>{true, false, true}))
        << "spacing is along the time axis, so a fine square's top/bottom points are crowded too; a cluster of 3 "
           "keeps its ends";
    EXPECT_EQ(visibleHandleMask({{0.0f, 0.0f}, {25.0f, 30.0f}}, 20.0f), (std::vector<bool>{true, true}));
    EXPECT_EQ(visibleHandleMask({{50.0f, 0.0f}, {50.0f, 30.0f}}, 20.0f), (std::vector<bool>{true, true}))
        << "two points at the same x (a step) both show";

    std::vector<juce::Point<float>> dense;
    for (int i = 0; i < 40; ++i)
        dense.push_back({(float)i * 2.0f, 0.0f});
    const auto mask = visibleHandleMask(dense, 20.0f);
    EXPECT_TRUE(mask.front());
    EXPECT_TRUE(mask.back());
    EXPECT_EQ(std::count(mask.begin(), mask.end(), true), 2) << "the middle of a dense run is thinned";

    dense.push_back({400.0f, 0.0f});
    dense.push_back({403.0f, 0.0f});
    const auto both = visibleHandleMask(dense, 20.0f);
    EXPECT_EQ(std::count(both.begin(), both.end(), true), 4) << "each cluster keeps its own ends";
}

TEST(AutomationLanesShapePaintTest, ADenseStampDrawsOnlyItsEndHandlesUntilZoomedIn) {
    PaintLane f;
    f.panel.getViewState().snap = TimelineViewState::Snap::Sixteenth;
    f.panel.setActiveTool(EditTool::Draw);
    f.panel.setDrawShape(DrawShape::Sine);
    dragAcross(*f.editor, {f.xAt(0.0), f.yAt(100.0)}, {f.xAt(4.0), f.yAt(0.0)}, 4);
    const auto count = f.doc.getLane(f.lane)->points.size();
    ASSERT_EQ(count, 16u * synth::ui::kSinePointsPerCycle + 1u);
    EXPECT_EQ(f.editor->visibleHandleCountForTest(), 2) << "257 points over 160 px read as a curve; only its ends show";

    // Hovering a hidden point draws its handle, so the point about to be grabbed is visible.
    f.panel.setActiveTool(EditTool::Select);
    const auto before = f.render();
    const auto target = f.editor->getHandleRectForTest(1.0).getCentre();
    f.editor->mouseMove(makeClickEvent(*f.editor, target.toFloat()));
    const auto after = f.render();
    bool changed = false;
    for (int dx = -4; dx <= 4 && !changed; ++dx)
        for (int dy = -4; dy <= 4 && !changed; ++dy)
            changed = before.getPixelAt(target.x + dx, target.y + dy) != after.getPixelAt(target.x + dx, target.y + dy);
    EXPECT_TRUE(changed) << "the hovered handle is painted";

    // Zoomed in far enough, every handle is back and still grabs.
    f.panel.getViewState().pixelsPerBeat = 4000.0;
    f.panel.getViewState().firstVisibleBeat = 0.0;
    EXPECT_EQ(f.editor->visibleHandleCountForTest(), (int)count);
    const double beat = 1.0 / 64.0; // the second sine point
    const auto handle = f.editor->getHandleRectForTest(beat).getCentre().toFloat();
    ASSERT_FALSE(handle.isOrigin());
    double before64 = 0.0;
    for (const auto& bp : f.doc.getLane(f.lane)->points)
        if (bp.beat == beat)
            before64 = bp.value;
    f.panel.getViewState().snapEnabled = false;
    dragAcross(*f.editor, handle, {handle.x, f.yAt(10.0)}, 4);
    bool moved = false;
    for (const auto& bp : f.doc.getLane(f.lane)->points)
        if (std::abs(bp.beat - beat) < 0.001) // the release x is the rounded handle centre
            moved = std::abs(bp.value - before64) > 20.0;
    EXPECT_TRUE(moved) << "a zoomed-in handle is hit and dragged";
}

// Regression test for FRO1195: "Right after" puts the first copy one tick past the last original; both dots show.
TEST(AutomationLanesShapePaintTest, DuplicatedPointsRightAfterShowBothDotsAtTheJoint) {
    PaintLane f;
    f.panel.getViewState().snapEnabled = false;
    for (const auto& [beat, value] : std::vector<std::pair<double, double>>{{1.0, 20.0}, {2.0, 60.0}, {3.0, 40.0}})
        f.doc.addBreakpoint(f.lane, beat, value);
    ASSERT_EQ(f.editor->visibleHandleCountForTest(), 3);
    f.editor->getPointSelection().setSelection({2.0, 3.0});
    ASSERT_TRUE(f.editor->duplicateSelectedPoints());
    f.editor->getPointSelection().clear(); // nothing selected: only the density mask decides what is drawn
    ASSERT_EQ(f.doc.getLane(f.lane)->points.size(), 5u);
    EXPECT_EQ(f.editor->visibleHandleCountForTest(), 5) << "the last original and the first copy both show";
}

TEST(AutomationLanesShapePaintTest, DraggingTheRightDotOfAPairPastTheLeftOneKeepsBothPainted) {
    PaintLane f;
    f.panel.getViewState().snapEnabled = false;
    f.doc.addBreakpoint(f.lane, 1.0, 20.0);
    f.doc.addBreakpoint(f.lane, 3.0, 40.0);
    f.doc.addBreakpoint(f.lane, 3.0 + synth::ui::tickBeats(), 70.0);
    f.editor->setTool(AutomationLaneEditor::Tool::Pointer);
    ASSERT_EQ(f.editor->visibleHandleCountForTest(), 3);

    const auto grab = juce::Point<float>{f.xAt(3.0 + synth::ui::tickBeats()), f.yAt(70.0)};
    f.editor->mouseDown(makeClickEvent(*f.editor, grab, leftButton()));
    // Left past the other dot (12 px), then back and on to 25 px right of the pair.
    const auto dragTo = [&](float dx) {
        f.editor->mouseDrag(makeDragEvent(*f.editor, {grab.x + dx, grab.y}, grab, leftButton()));
    };
    for (int i = 1; i <= 8; ++i) {
        dragTo(i <= 4 ? -3.0f * (float)i : -12.0f + 9.25f * (float)(i - 4));
        EXPECT_EQ(f.editor->visibleHandleCountForTest(), 3) << "step " << i;
    }
    f.editor->mouseUp(makeDragEvent(*f.editor, {grab.x + 25.0f, grab.y}, grab, leftButton()));
    f.editor->getPointSelection().clear();
    EXPECT_EQ(f.editor->visibleHandleCountForTest(), 3);
}
