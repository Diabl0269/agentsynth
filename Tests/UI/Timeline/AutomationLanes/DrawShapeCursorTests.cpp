// DrawShapeCursorTests.cpp -- the pointer over a drawing area shows the selected draw shape: on an automation lane and
// in the shape window's curve, the periodic pens (Sine, Triangle, Saw, Square) show their own icon with the hot spot on
// the start of the stroke, Free and Line keep the pen, Select keeps the arrow, a change of shape shows at once, and a
// theme change re-tints the icon.

#include "AutomationLanesTestFixture.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeIcons.h"
#include "UI/Timeline/AutomationLanes/LaneToolCursors.h"
#include "UI/Timeline/ToolCursors.h"

using namespace automation_lanes_test;
using namespace synth::ui;

namespace {

const std::array<DrawShape, 4> kPeriodic{DrawShape::Sine, DrawShape::Triangle, DrawShape::Saw, DrawShape::Square};

juce::String expectedKind(DrawShape shape) {
    return isPeriodicShape(shape) ? "tool:" + juce::String(drawShapeName(shape)).toLowerCase() : juce::String("tool");
}

// The most opaque pixel of a cursor's picture: the icon's own colour.
juce::Colour inkOf(const juce::MouseCursor& cursor) {
    const auto* entry = findToolCursorImage(cursor);
    if (entry == nullptr)
        return {};
    juce::Colour best;
    for (int y = 0; y < entry->image.getHeight(); ++y)
        for (int x = 0; x < entry->image.getWidth(); ++x)
            if (const auto pixel = entry->image.getPixelAt(x, y); pixel.getAlpha() > best.getAlpha())
                best = pixel;
    return best;
}

struct LaneUnderTest : LanesPanel {
    synth::LaneId lane;
    AutomationLaneEditor* editor = nullptr;

    LaneUnderTest() {
        const auto track = doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane = addLane(track, "cutoff");
        panel.setTrackAutomationExpanded(track, true);
        editor = panel.laneEditorForTest(lane);
    }
};

} // namespace

TEST(DrawShapeCursor, ALaneShowsTheSelectedShapesIconUnderDrawAndTheArrowUnderSelect) {
    LaneUnderTest f;
    ASSERT_NE(f.editor, nullptr);
    f.panel.setActiveTool(EditTool::Draw);
    for (const auto shape : kAllDrawShapes) {
        f.editor->setDrawShape(shape);
        EXPECT_EQ(laneCursorKind(f.editor->getMouseCursor()), expectedKind(shape)) << drawShapeName(shape);
    }
    f.editor->setDrawShape(DrawShape::Sine);
    f.panel.setActiveTool(EditTool::Select);
    EXPECT_EQ(laneCursorKind(f.editor->getMouseCursor()), "arrow");
    f.panel.setActiveTool(EditTool::Erase);
    EXPECT_EQ(laneCursorKind(f.editor->getMouseCursor()), "tool") << "other tools keep their own cursor";
}

TEST(DrawShapeCursor, AChangeOfShapeChangesTheLaneCursorWithoutAMouseMove) {
    LaneUnderTest f;
    ASSERT_NE(f.editor, nullptr);
    f.panel.setActiveTool(EditTool::Draw);
    f.editor->setDrawShape(DrawShape::Sine);
    const auto sine = f.editor->getMouseCursor();
    f.editor->setDrawShape(DrawShape::Square);
    EXPECT_FALSE(f.editor->getMouseCursor() == sine);
    EXPECT_EQ(laneCursorKind(f.editor->getMouseCursor()), "tool:square");
}

TEST(DrawShapeCursor, TheShapeWindowsCurveShowsTheSameCursorAsALaneForEveryPen) {
    CurveEditorComponent curve;
    curve.setSize(200, 100);
    EXPECT_EQ(laneCursorKind(curve.getMouseCursor()), "arrow");
    for (const auto shape : kAllDrawShapes) {
        curve.setDrawPen(shape);
        EXPECT_EQ(laneCursorKind(curve.getMouseCursor()), expectedKind(shape)) << drawShapeName(shape);
    }
    curve.setDrawPen(DrawShape::Triangle);
    const auto triangle = curve.getMouseCursor();
    curve.setDrawPen(DrawShape::Saw);
    EXPECT_FALSE(curve.getMouseCursor() == triangle) << "a new pen shows at once";
    curve.setDrawPen(std::nullopt);
    EXPECT_EQ(laneCursorKind(curve.getMouseCursor()), "arrow") << "the Select pen is the arrow";
}

TEST(DrawShapeCursor, TheHotSpotIsWhereTheStrokeStarts) {
    juce::Component owner;
    LaneToolCursors cursors;
    for (const auto shape : kPeriodic) {
        const auto path = drawShapeIconPath(shape);
        juce::Path::Iterator first(path);
        first.next();
        const auto* entry = findToolCursorImage(cursors.get(owner, EditTool::Draw, shape));
        ASSERT_NE(entry, nullptr) << drawShapeName(shape);
        EXPECT_EQ(entry->hotspot, juce::Point<int>(juce::roundToInt(first.x1), juce::roundToInt(first.y1)))
            << drawShapeName(shape);
        EXPECT_GT(inkOf(entry->cursor).getAlpha(), 0) << "the icon is drawn";
    }
}

TEST(DrawShapeCursor, AThemeChangeReTintsTheIconInEveryBuiltInTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        synth::theme::AppLookAndFeel lf;
        lf.applyTheme(theme);
        CurveEditorComponent curve;
        curve.setSize(200, 100);
        curve.setDrawPen(DrawShape::Sine);
        curve.setLookAndFeel(&lf); // lookAndFeelChanged: the cursor is rebuilt from this theme
        const auto ink = inkOf(curve.getMouseCursor());
        EXPECT_NEAR(ink.getRed(), theme.colors.textPrimary.getRed(), 2) << theme.id;
        EXPECT_NEAR(ink.getGreen(), theme.colors.textPrimary.getGreen(), 2) << theme.id;
        EXPECT_NEAR(ink.getBlue(), theme.colors.textPrimary.getBlue(), 2) << theme.id;
        curve.setLookAndFeel(nullptr);
    }
}

TEST(DrawShapeCursor, InvalidateRebuildsTheCursorsFromTheCurrentTheme) {
    synth::theme::AppLookAndFeel lf;
    juce::Component owner;
    owner.setLookAndFeel(&lf);
    LaneToolCursors cursors;
    const auto themes = synth::theme::builtInThemes();
    ASSERT_GE(themes.size(), 2u);
    lf.applyTheme(themes[0]);
    const auto first = cursors.get(owner, EditTool::Draw, DrawShape::Square);
    lf.applyTheme(themes[1]);
    EXPECT_TRUE(cursors.get(owner, EditTool::Draw, DrawShape::Square) == first) << "built once until invalidated";
    cursors.invalidate();
    EXPECT_FALSE(cursors.get(owner, EditTool::Draw, DrawShape::Square) == first);
    owner.setLookAndFeel(nullptr);
}
