// ShapeEditorTests.cpp -- the Filter shape editor window's content: selection, the curve editor driven through real
// mouse events, the buttons, Start from, the lane draw shapes, undo and Escape.
#include "../../Accessibility/TabOrderHelpers.h"
#include "Modules/Filter/CustomFilterShapes.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"
#include "UI/ModuleViews/ShapeEditor/ShapeEditorWindow.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeFlyout.h"

using namespace synth::ui;
using namespace synth::ui::test;
using synth::CustomFilterShapes;

namespace {

CurveGeometryConfig editorGeometryConfig() {
    CurveGeometryConfig config;
    config.explicitVisibleRange = 1.0;
    config.zeroSegmentPx = 0.0f;
    return config;
}

// Button::triggerClick posts its click to the message queue, so a test runs the click handler itself.
void click(juce::Button& button) { button.onClick(); }

} // namespace

TEST(ShapeEditor, ShowsOneChipPerShapeAndTheSelectedShapesCurve) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 1);

    EXPECT_EQ(editor.getNumShapes(), 3);
    EXPECT_NE(editor.getChip(2), nullptr);
    EXPECT_EQ(editor.getChip(3), nullptr);
    EXPECT_EQ(editor.getSelected(), 1);
    EXPECT_EQ(editor.getCurveEditor().getModel().getNumNodes(),
              (int)CustomFilterShapes::defaultShapes().shapes[1].curve.points.size());
    EXPECT_EQ(editor.getWindowTitle(), "Filter shapes: Filter");
}

TEST(ShapeEditor, ClickingAChipSelectsThatShapeEverywhere) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);

    click(*static_cast<juce::Button*>(editor.getChip(2)));

    EXPECT_EQ(editor.getSelected(), 2);
    EXPECT_EQ(editor.getMap().getSelected(), 2);
    EXPECT_EQ(editor.getCurveEditor().getModel().getNumNodes(),
              (int)CustomFilterShapes::defaultShapes().shapes[2].curve.points.size());
}

TEST(ShapeEditor, SelectingADotOnTheMapSelectsItsChipAndCurve) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& map = editor.getMap();
    map.setBounds(0, 0, 340, 340);
    const auto at = map.getDotCentre(1);

    map.mouseDown(curveLeftClick(map, at));
    map.mouseUp(curveLeftClick(map, at));

    EXPECT_EQ(editor.getSelected(), 1);
}

TEST(ShapeEditor, DraggingACurvePointIsHeardLiveAndIsOneUndoStep) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& curve = editor.getCurveEditor();
    ASSERT_GT(curve.getWidth(), 0);
    const CurveEditorGeometry geometry(curve.getModel(), curve.getLocalBounds().toFloat(), editorGeometryConfig());
    const int node = 3;
    const auto from = geometry.nodePosition(node);
    const auto to = from + juce::Point<float>(0.0f, -30.0f);
    const int generationBefore = rig.filter->getCustomShapesGeneration();
    const int serialBefore = rig.undo.getEditSerial();

    curve.mouseDown(curveLeftClick(curve, from));
    curve.mouseDrag(curveLeftDrag(curve, to, from));
    EXPECT_GT(rig.filter->getCustomShapesGeneration(), generationBefore) << "the module has the edit mid-drag";
    curve.mouseUp(curveLeftDrag(curve, to, from));

    const float yAfter = rig.filter->getCustomShapes().shapes[0].curve.points[(size_t)node].y;
    EXPECT_GT(yAfter, CustomFilterShapes::defaultShapes().shapes[0].curve.points[(size_t)node].y);
    EXPECT_EQ(rig.undo.getEditSerial(), serialBefore + 1) << "a whole drag is one undo step";

    rig.undo.undo();
    EXPECT_TRUE(rig.filter->getCustomShapes().isDefault());
    editor.refresh();
    EXPECT_EQ(editor.getCurveEditor().getModel().getNode(node).y,
              CustomFilterShapes::defaultShapes().shapes[0].curve.points[(size_t)node].y)
        << "the window follows an undo";
}

TEST(ShapeEditor, AddShapeDuplicateAndDeleteEachAreOneUndoStep) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    const int serial = rig.undo.getEditSerial();

    click(editor.getAddButton());
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 4u);
    EXPECT_EQ(editor.getSelected(), 3) << "the new shape is selected";
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);

    click(editor.getDuplicateButton());
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 5u);
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[4].curve, rig.filter->getCustomShapes().shapes[3].curve);
    EXPECT_NE(rig.filter->getCustomShapes().shapes[4].name, rig.filter->getCustomShapes().shapes[3].name);

    click(editor.getDeleteButton());
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 4u);

    rig.undo.undo();
    rig.undo.undo();
    rig.undo.undo();
    EXPECT_TRUE(rig.filter->getCustomShapes().isDefault());
}

TEST(ShapeEditor, DeleteLeavesAtLeastOneShapeAndDisablesItsButton) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);

    click(editor.getDeleteButton());
    click(editor.getDeleteButton());
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 1u);
    EXPECT_FALSE(editor.getDeleteButton().isEnabled());
    click(editor.getDeleteButton());
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 1u);
}

TEST(ShapeEditor, RenameButtonEditsTheSelectedShapesNameInPlace) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 1);

    click(editor.getRenameButton());
    ASSERT_TRUE(editor.getMap().isRenaming());
    // The inline editor commits through the map's rename path.
    editor.getMap().onRenameDot(1, "bright");
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[1].name, "bright");
    ASSERT_NE(editor.getChip(1), nullptr);
    EXPECT_EQ(static_cast<juce::Button*>(editor.getChip(1))->getButtonText(), "bright");
}

TEST(ShapeEditor, StartFromListsTheVowelsAndAnimalsAndStampsTheCurveOnTheSelectedShape) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 2);
    CapturedMenu captured;
    editor.showMenu = [&](juce::PopupMenu& m) { captured.capture(m); };

    click(editor.getStartFromButton());

    const std::vector<std::string> expected{"ah", "ee", "oo", "wolf", "crow", "frog"};
    EXPECT_EQ(captured.items, expected);
    const int serial = rig.undo.getEditSerial();
    ASSERT_TRUE(captured.run("wolf"));
    const auto& shape = rig.filter->getCustomShapes().shapes[2];
    EXPECT_EQ(shape.curve, CustomFilterShapes::presetCurve(CustomFilterShapes::Preset::Wolf));
    EXPECT_EQ(shape.name, "oo") << "only the curve is replaced";
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);

    rig.undo.undo();
    EXPECT_TRUE(rig.filter->getCustomShapes().isDefault());
}

TEST(ShapeEditor, TheLaneDrawFlyoutShapesTheSelectedCurve) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    std::unique_ptr<DrawShapeFlyout> flyout;
    test_hooks::drawShapeFlyoutHookForTest() = [&](std::unique_ptr<DrawShapeFlyout> f) { flyout = std::move(f); };

    click(editor.getDrawButton());
    test_hooks::drawShapeFlyoutHookForTest() = nullptr;
    ASSERT_NE(flyout, nullptr);

    flyout->pick(DrawShape::Sine);
    const auto drawn = rig.filter->getCustomShapes().shapes[0].curve;
    const auto range = rig.adapter->getDrawRange();
    EXPECT_GT(drawn.points.size(), 20u) << "a sine across the audible range has many points";
    for (const auto& p : drawn.points) {
        EXPECT_GE(p.y, range.lo - 0.001f);
        EXPECT_LE(p.y, range.hi + 0.001f);
    }
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[1], CustomFilterShapes::defaultShapes().shapes[1])
        << "other shapes are untouched";

    rig.undo.undo();
    EXPECT_TRUE(rig.filter->getCustomShapes().isDefault());
}

TEST(ShapeEditor, SquareDrawMakesVerticalStepsAndLineJoinsTheEnds) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);

    editor.applyDrawShape(DrawShape::Square);
    const auto square = rig.filter->getCustomShapes().shapes[0].curve;
    int steps = 0;
    for (size_t i = 1; i < square.points.size(); ++i)
        steps += square.points[i].x == square.points[i - 1].x ? 1 : 0;
    EXPECT_GE(steps, 4) << "each hold becomes a step";

    editor.applyDrawShape(DrawShape::Line);
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[0].curve.points.size(), 2u);

    const auto before = rig.filter->getCustomShapes().shapes[0].curve;
    editor.applyDrawShape(DrawShape::Free);
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[0].curve, before) << "Free draws nothing";
}

TEST(ShapeEditor, CurveAxesAreLabelledInHertzAndDecibelsWithAZeroDbLine) {
    ShapeRig rig;
    const auto guides = rig.adapter->getAxisGuides();
    ASSERT_TRUE(guides.has_value());
    ASSERT_EQ(guides->x.size(), 3u);
    EXPECT_EQ(guides->x[0].label, "100");
    EXPECT_EQ(guides->x[1].label, "1k");
    EXPECT_EQ(guides->x[2].label, "10k");
    EXPECT_NEAR(guides->x[1].position, CustomFilterShapes::hzToX(1000.0f), 1e-6);
    bool zero = false;
    for (const auto& g : guides->y)
        if (g.emphasised) {
            zero = true;
            EXPECT_NEAR(g.position, 0.8, 1e-6) << "0 dB sits at y = 0.8";
        }
    EXPECT_TRUE(zero);
}

TEST(ShapeEditor, EscapeAndDoneClose) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    int closed = 0;
    editor.onDone = [&] { ++closed; };

    EXPECT_TRUE(editor.keyPressed(key(juce::KeyPress::escapeKey)));
    EXPECT_EQ(closed, 1);
    click(editor.getDoneButton());
    EXPECT_EQ(closed, 2);
    EXPECT_FALSE(editor.keyPressed(key('x')));
}

TEST(ShapeEditor, TheBottomBarKeepsASlotForPerNoteSettings) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    EXPECT_FALSE(editor.getBottomBarSlot().getBounds().isEmpty());
    EXPECT_TRUE(editor.getLocalBounds().contains(editor.getBottomBarSlot().getBounds()));
    EXPECT_FALSE(editor.getBottomBarSlot().getBounds().intersects(editor.getDoneButton().getBounds()));
}

TEST(ShapeEditor, AnEditMadeElsewhereReachesTheWindowOnRefresh) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto shapes = rig.filter->getCustomShapes();
    shapes.shapes.push_back(shapes.shapes[0]);
    shapes.shapes.back().name = "extra";
    rig.filter->setCustomShapes(shapes);

    editor.refresh();

    EXPECT_EQ(editor.getNumShapes(), 4);
    ASSERT_NE(editor.getChip(3), nullptr);
    EXPECT_EQ(static_cast<juce::Button*>(editor.getChip(3))->getButtonText(), "extra");
}

TEST(ShapeEditor, TheWindowHookReceivesTheEditorOnTheRequestedShape) {
    ShapeRig rig;
    std::unique_ptr<ShapeListEditor> received;
    test_hooks::shapeEditorWindowHookForTest() = [&](std::unique_ptr<ShapeListEditor> e) { received = std::move(e); };

    auto* window = openShapeEditorWindow(rig.adapter, 2, nullptr);
    test_hooks::shapeEditorWindowHookForTest() = nullptr;

    EXPECT_EQ(window, nullptr);
    ASSERT_NE(received, nullptr);
    EXPECT_EQ(received->getSelected(), 2);
}

TEST(ShapeEditor, TabVisitsTheMapChipsCurveButtonsAndDoneInVisualOrder) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    const auto walk = synth::test::walkTabOrder(editor);
    std::vector<std::string> names;
    for (auto* c : walk.forward)
        names.push_back(c->getTitle().toStdString());
    const std::vector<std::string> expected{"Morph map",
                                            "ah, X 0.20, Y 0.25",
                                            "ee, X 0.80, Y 0.25",
                                            "oo, X 0.50, Y 0.80",
                                            "Morph cursor, X 0.50, Y 0.50",
                                            "Morph X, 0.50",
                                            "Morph Y, 0.50",
                                            "ah shape",
                                            "ee shape",
                                            "oo shape",
                                            "Shape curve",
                                            "Add shape",
                                            "Duplicate",
                                            "Delete",
                                            "Rename",
                                            "Start from",
                                            "Draw",
                                            "Done"};
    EXPECT_EQ(names, expected);
    EXPECT_EQ(walk.backward.size(), walk.forward.size());
    for (auto* c : walk.forward) {
        auto* tip = dynamic_cast<juce::TooltipClient*>(c);
        ASSERT_NE(tip, nullptr) << c->getTitle();
        EXPECT_FALSE(tip->getTooltip().isEmpty()) << c->getTitle();
    }
}
