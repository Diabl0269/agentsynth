// AutomationLanesShapePaintTests.cpp -- what a lane draws around the Draw shapes: the shape previewed
// inside the box mid-drag (read back from rendered pixels), and crowded point handles left out so a dense
// stamp reads as a curve, coming back on zoom-in and still grabbable there.

#include "AutomationLanesTestFixture.h"
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

TEST(AutomationLanesShapePaintTest, CrowdedNeighboursHideEachOthersHandles) {
    using synth::ui::visibleHandleMask;
    const auto mask = visibleHandleMask({{0.0f, 10.0f}, {40.0f, 10.0f}, {45.0f, 10.0f}, {90.0f, 10.0f}}, 20.0f);
    EXPECT_EQ(mask, (std::vector<bool>{true, false, false, true}));
    EXPECT_EQ(visibleHandleMask({{0.0f, 0.0f}, {5.0f, 30.0f}}, 20.0f), (std::vector<bool>{true, true}))
        << "distance is on screen, so a steep step keeps both";
}

TEST(AutomationLanesShapePaintTest, ADenseStampDrawsNoHandlesUntilZoomedIn) {
    PaintLane f;
    f.panel.getViewState().snap = TimelineViewState::Snap::Sixteenth;
    f.panel.setActiveTool(EditTool::Draw);
    f.panel.setDrawShape(DrawShape::Sine);
    dragAcross(*f.editor, {f.xAt(0.0), f.yAt(100.0)}, {f.xAt(4.0), f.yAt(0.0)}, 4);
    const auto count = f.doc.getLane(f.lane)->points.size();
    ASSERT_EQ(count, 16u * synth::ui::kSinePointsPerCycle + 1u);
    EXPECT_EQ(f.editor->visibleHandleCountForTest(), 0) << "257 points over 160 px read as a curve";

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
