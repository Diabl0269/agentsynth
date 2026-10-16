// MorphMapTests.cpp -- the morph map through real mouse and key events: cursor and dot drags, click-to-add, the dot
// menu, Delete, the cable edge bands, the tooltips, the screen-reader names and the Tab order.
#include "../../Accessibility/TabOrderHelpers.h"
#include "Modules/Filter/CustomFilterBlend.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/MorphMap/MorphMapPalette.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

struct MapRig : ShapeRig {
    MorphMap map;
    ShapeMapController controller{map, *adapter};
    CapturedMenu menu;
    int gestureStarts = 0;
    int gestureEnds = 0;

    MapRig() {
        map.setSize(252, 150);
        map.showMenu = [this](juce::PopupMenu& m) { menu.capture(m); };
        controller.refresh();
    }
};

void drag(MorphMap& map, juce::Point<float> from, juce::Point<float> to) {
    map.mouseDown(curveLeftClick(map, from));
    map.mouseDrag(curveLeftDrag(map, to, from));
    map.mouseUp(curveLeftDrag(map, to, from));
}

void click(MorphMap& map, juce::Point<float> at) {
    map.mouseDown(curveLeftClick(map, at));
    map.mouseUp(curveLeftClick(map, at));
}

} // namespace

TEST(MorphMap, ShowsOneDotPerShapeAndTheCursorAtTheModulesMorphPosition) {
    MapRig rig;
    EXPECT_EQ(rig.map.getNumDots(), 3);
    EXPECT_NEAR(rig.map.getCursor().x, 0.5f, 1e-5);
    EXPECT_NEAR(rig.map.getCursor().y, 0.5f, 1e-5);
    EXPECT_EQ(rig.map.getDots()[0].name, "ah");
    EXPECT_NEAR(rig.map.getDots()[2].x, 0.5f, 1e-5);
    EXPECT_NEAR(rig.map.getDots()[2].y, 0.8f, 1e-5);
}

TEST(MorphMap, DraggingTheCursorSetsMorphXAndYInOneHostGestureAndOneUndoStep) {
    MapRig rig;
    int begins = 0, ends = 0;
    struct Listener : juce::AudioProcessorParameter::Listener {
        int* begins;
        int* ends;
        void parameterValueChanged(int, float) override {}
        void parameterGestureChanged(int, bool starting) override { ++*(starting ? begins : ends); }
    } listener;
    listener.begins = &begins;
    listener.ends = &ends;
    auto* morphX = findParameterByID(rig.filter, "morphX");
    morphX->addListener(&listener);
    const int serial = rig.undo.getEditSerial();

    const auto from = rig.map.getCursorCentre();
    const auto to = rig.map.toPixels(0.25f, 0.75f);
    drag(rig.map, from, to);
    morphX->removeListener(&listener);

    EXPECT_NEAR(rig.filter->getCurrentMorphX(), 0.25f, 0.02f);
    EXPECT_NEAR(rig.filter->getCurrentMorphY(), 0.75f, 0.02f);
    EXPECT_EQ(begins, 1) << "automation sees one gesture, not one per mouse move";
    EXPECT_EQ(ends, 1);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
}

TEST(MorphMap, ClickingEmptySpaceAddsAShapeThatCopiesTheCurrentBlend) {
    MapRig rig;
    const auto at = rig.map.toPixels(0.9f, 0.9f);

    click(rig.map, at);

    const auto& shapes = rig.filter->getCustomShapes().shapes;
    ASSERT_EQ(shapes.size(), 4u);
    EXPECT_NEAR(shapes[3].position[0], 0.9f, 0.02f);
    EXPECT_NEAR(shapes[3].position[1], 0.9f, 0.02f);
    EXPECT_GE(shapes[3].curve.points.size(), 12u);
    EXPECT_LE(shapes[3].curve.points.size(), 20u);
    // The copy is the blend at the cursor (0.5, 0.5): its dB at 1 kHz matches the module's own blend there.
    const auto blend = synth::customfilter::blendedGridDb(synth::CustomFilterShapes::defaultShapes(), 0.5f, 0.5f);
    const float x = synth::CustomFilterShapes::hzToX(1000.0f);
    const float copyDb = synth::CustomFilterShapes::yToDb(shapes[3].curve.evaluate(x));
    EXPECT_NEAR(copyDb, blend[(size_t)(x * (synth::customfilter::kGridSize - 1))], 2.0f);
    EXPECT_EQ(rig.undo.getEditSerial(), 1);
}

TEST(MorphMap, AClickOnADotAddsNothingAndSelectsIt) {
    MapRig rig;
    click(rig.map, rig.map.getDotCentre(2));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 3u);
    EXPECT_EQ(rig.map.getSelected(), 2);
}

TEST(MorphMap, DraggingADotMovesItsPositionAsOneUndoStep) {
    MapRig rig;
    const auto from = rig.map.getDotCentre(0);
    const auto to = rig.map.toPixels(0.4f, 0.55f);
    const int serial = rig.undo.getEditSerial();

    drag(rig.map, from, to);

    const auto& position = rig.filter->getCustomShapes().shapes[0].position;
    EXPECT_NEAR(position[0], 0.4f, 0.02f);
    EXPECT_NEAR(position[1], 0.55f, 0.02f);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    EXPECT_EQ(rig.filter->getCustomShapes().shapes[0].curve,
              synth::CustomFilterShapes::defaultShapes().shapes[0].curve);
    rig.undo.undo();
    EXPECT_TRUE(rig.filter->getCustomShapes().isDefault());
}

TEST(MorphMap, DeleteKeyRemovesTheFocusedDotButOneAlwaysStays) {
    MapRig rig;
    auto* stop = rig.map.getDotStop(0);
    ASSERT_NE(stop, nullptr);

    EXPECT_TRUE(stop->keyPressed(key(juce::KeyPress::deleteKey)));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 2u);
    EXPECT_TRUE(rig.map.getDotStop(0)->keyPressed(key(juce::KeyPress::deleteKey)));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 1u);
    rig.map.getDotStop(0)->keyPressed(key(juce::KeyPress::deleteKey));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 1u) << "the last shape stays";
}

TEST(MorphMap, RightClickOnADotOpensEditDuplicateRenameDeleteAndEachWorks) {
    MapRig rig;
    int edited = -1;
    rig.controller.onEdit = [&](int i) { edited = i; };
    const auto at = rig.map.getDotCentre(1);

    rig.map.mouseDown(curveRightClick(rig.map, at));
    const std::vector<std::string> expected{"Edit", "Duplicate", "Rename", "Delete"};
    EXPECT_EQ(rig.menu.items, expected);

    ASSERT_TRUE(rig.menu.run("Edit"));
    EXPECT_EQ(edited, 1);
    ASSERT_TRUE(rig.menu.run("Duplicate"));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 4u);
    ASSERT_TRUE(rig.menu.run("Rename"));
    EXPECT_TRUE(rig.map.isRenaming());
    ASSERT_TRUE(rig.menu.run("Delete"));
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 3u);
}

TEST(MorphMap, DeleteInTheMenuIsDisabledOnTheLastShape) {
    MapRig rig;
    rig.controller.deleteShape(0);
    rig.controller.deleteShape(0);
    ASSERT_EQ(rig.filter->getCustomShapes().shapes.size(), 1u);

    rig.map.mouseDown(curveRightClick(rig.map, rig.map.getDotCentre(0)));

    EXPECT_FALSE(rig.menu.isEnabled("Delete"));
}

TEST(MorphMap, DoubleClickOnADotAndReturnOnAFocusedDotAskToEditIt) {
    MapRig rig;
    int edited = -1;
    rig.controller.onEdit = [&](int i) { edited = i; };

    rig.map.mouseDoubleClick(curveDoubleClick(rig.map, rig.map.getDotCentre(2)));
    EXPECT_EQ(edited, 2);

    edited = -1;
    EXPECT_TRUE(rig.map.getDotStop(1)->keyPressed(key(juce::KeyPress::returnKey)));
    EXPECT_EQ(edited, 1);

    edited = -1;
    rig.map.mouseDoubleClick(curveDoubleClick(rig.map, rig.map.toPixels(0.95f, 0.05f)));
    EXPECT_EQ(edited, -1) << "empty space edits nothing";
}

TEST(MorphMap, ArrowKeysMoveTheFocusedDotAndTheCursorShiftForBiggerSteps) {
    MapRig rig;
    const float x0 = rig.map.getDots()[0].x;
    rig.map.getDotStop(0)->keyPressed(key(juce::KeyPress::rightKey));
    EXPECT_NEAR(rig.filter->getCustomShapes().shapes[0].position[0], x0 + MorphMap::kStep, 1e-4);
    rig.map.getDotStop(0)->keyPressed(key(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier));
    EXPECT_NEAR(rig.filter->getCustomShapes().shapes[0].position[0], x0 + MorphMap::kStep + MorphMap::kBigStep, 1e-4);

    const float y0 = rig.filter->getCurrentMorphY();
    const int serial = rig.undo.getEditSerial();
    rig.map.getCursorStop()->keyPressed(key(juce::KeyPress::upKey));
    EXPECT_NEAR(rig.filter->getCurrentMorphY(), y0 - MorphMap::kStep, 1e-4);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1) << "one key step is one undo step";
}

TEST(MorphMap, ArrowsStopAtTheMapEdge) {
    MapRig rig;
    for (int i = 0; i < 80; ++i)
        rig.map.getCursorStop()->keyPressed(key(juce::KeyPress::leftKey));
    EXPECT_FLOAT_EQ(rig.filter->getCurrentMorphX(), 0.0f);
}

TEST(MorphMap, TabVisitsTheMapThenEachDotThenTheCursorThenTheSliders) {
    MapRig rig;
    juce::Component host;
    host.setSize(300, 200);
    host.addAndMakeVisible(rig.map);
    rig.map.setBounds(host.getLocalBounds());

    const auto walk = synth::test::walkTabOrder(host);
    ASSERT_EQ(walk.forward.size(), 7u);
    EXPECT_EQ(walk.forward[0], &rig.map);
    EXPECT_EQ(walk.forward[1], rig.map.getDotStop(0));
    EXPECT_EQ(walk.forward[2], rig.map.getDotStop(1));
    EXPECT_EQ(walk.forward[3], rig.map.getDotStop(2));
    EXPECT_EQ(walk.forward[4], rig.map.getCursorStop());
    EXPECT_EQ(walk.forward[5], rig.map.getStripStop(MorphMap::Axis::X));
    EXPECT_EQ(walk.forward[6], rig.map.getStripStop(MorphMap::Axis::Y));
    EXPECT_EQ(walk.backward.size(), walk.forward.size());
}

TEST(MorphMap, StopsAreNamedWithTheirValuesAndTooltipped) {
    MapRig rig;
    rig.filter->setCustomShapes(rig.filter->getCustomShapes());
    EXPECT_EQ(rig.map.getCursorStop()->getTitle(), "Morph cursor, X 0.50, Y 0.50");
    EXPECT_EQ(rig.map.getDotStop(0)->getTitle(), "ah, X 0.20, Y 0.25");
    rig.map.getCursorStop()->keyPressed(key(juce::KeyPress::rightKey));
    EXPECT_EQ(rig.map.getCursorStop()->getTitle(), "Morph cursor, X 0.52, Y 0.50");

    EXPECT_EQ(rig.map.tooltipAt(rig.map.getCursorCentre().roundToInt()), "Morph cursor - drag to blend shapes");
    EXPECT_EQ(rig.map.tooltipAt(rig.map.getDotCentre(1).roundToInt()), "ee - drag to move, double-click to edit");
    EXPECT_TRUE(rig.map.tooltipAt(rig.map.toPixels(0.95f, 0.05f).roundToInt()).startsWith("Morph map"));
    EXPECT_EQ(rig.map.getTitle(), "Morph map");
    for (auto* c : {rig.map.getDotStop(0), rig.map.getDotStop(1), rig.map.getCursorStop()}) {
        auto* tip = dynamic_cast<juce::SettableTooltipClient*>(c);
        ASSERT_NE(tip, nullptr);
        EXPECT_FALSE(tip->getTooltip().isEmpty());
    }
}

TEST(MorphMap, SlidersHugTheBottomAndLeftEdgesAndTheRightEdgeStaysFree) {
    MapRig rig;
    const auto x = rig.map.getStripBounds(MorphMap::Axis::X);
    const auto y = rig.map.getStripBounds(MorphMap::Axis::Y);
    const auto plot = rig.map.getPlotArea();
    EXPECT_GE(x.getY(), plot.getBottom());
    EXPECT_LE(x.getHeight(), 10.0f);
    EXPECT_LE(y.getRight(), plot.getX());
    EXPECT_LE(y.getWidth(), 10.0f);
    // Symmetric about the middle, so a future right-hand slider fits without moving the map.
    EXPECT_NEAR(plot.getX(), (float)rig.map.getWidth() - plot.getRight(), 0.01f);
    EXPECT_EQ(rig.map.stripAt(x.getCentre()), MorphMap::Axis::X);
    EXPECT_EQ(rig.map.stripAt(y.getCentre()), MorphMap::Axis::Y);
    EXPECT_EQ(rig.map.stripAt({(float)rig.map.getWidth() - 4.0f, (float)rig.map.getHeight() * 0.5f}),
              MorphMap::Axis::None);
    EXPECT_EQ(rig.map.stripAt(plot.getCentre()), MorphMap::Axis::None);
    EXPECT_GT(rig.map.stripAnchor(MorphMap::Axis::X).y, x.getBottom() - 1.0f);
    EXPECT_LT(rig.map.stripAnchor(MorphMap::Axis::Y).x, y.getX() + 1.0f);
    EXPECT_EQ(rig.map.getStripBounds(MorphMap::Axis::None), juce::Rectangle<float>());
    EXPECT_TRUE(rig.map.setDropHighlight(MorphMap::Axis::Y));
    EXPECT_FALSE(rig.map.setDropHighlight(MorphMap::Axis::Y));
}

TEST(MorphMap, DraggingTheMorphXSliderSetsOnlyMorphXInOneHostGestureAndUndoStep) {
    MapRig rig;
    int begins = 0, ends = 0;
    struct Listener : juce::AudioProcessorParameter::Listener {
        int* begins;
        int* ends;
        void parameterValueChanged(int, float) override {}
        void parameterGestureChanged(int, bool starting) override { ++*(starting ? begins : ends); }
    } listener;
    listener.begins = &begins;
    listener.ends = &ends;
    auto* morphX = findParameterByID(rig.filter, "morphX");
    morphX->addListener(&listener);
    const int serial = rig.undo.getEditSerial();
    const auto strip = rig.map.getStripBounds(MorphMap::Axis::X);
    const auto from = juce::Point<float>(strip.getX() + strip.getWidth() * 0.5f, strip.getCentreY());
    const auto to = juce::Point<float>(strip.getX() + strip.getWidth() * 0.9f, strip.getCentreY());

    drag(rig.map, from, to);
    morphX->removeListener(&listener);

    EXPECT_NEAR(rig.filter->getCurrentMorphX(), 0.9f, 0.02f);
    EXPECT_NEAR(rig.filter->getCurrentMorphY(), 0.5f, 1e-5);
    EXPECT_EQ(begins, 1);
    EXPECT_EQ(ends, 1);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
}

TEST(MorphMap, ClickingTheMorphYSliderJumpsTheCursorThereAndAddsNoShape) {
    MapRig rig;
    const auto strip = rig.map.getStripBounds(MorphMap::Axis::Y);
    click(rig.map, {strip.getCentreX(), strip.getY() + strip.getHeight() * 0.25f});
    EXPECT_NEAR(rig.filter->getCurrentMorphY(), 0.25f, 0.02f);
    EXPECT_EQ(rig.filter->getCustomShapes().shapes.size(), 3u);
}

TEST(MorphMap, SliderStopsTakeArrowKeysAndAreNamedWithTheirValue) {
    MapRig rig;
    auto* x = rig.map.getStripStop(MorphMap::Axis::X);
    auto* y = rig.map.getStripStop(MorphMap::Axis::Y);
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->getTitle(), "Morph X, 0.50");
    EXPECT_EQ(y->getTitle(), "Morph Y, 0.50");

    const int serial = rig.undo.getEditSerial();
    EXPECT_TRUE(x->keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_NEAR(rig.filter->getCurrentMorphX(), 0.5f + MorphMap::kStep, 1e-4);
    EXPECT_EQ(rig.filter->getCurrentMorphY(), 0.5f);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1);
    EXPECT_TRUE(y->keyPressed(key(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier)));
    EXPECT_NEAR(rig.filter->getCurrentMorphY(), 0.5f + MorphMap::kBigStep, 1e-4);
    EXPECT_TRUE(x->keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_EQ(x->getTitle(), "Morph X, 0.50");
    for (auto* c : {x, y}) {
        auto* tip = dynamic_cast<juce::SettableTooltipClient*>(c);
        ASSERT_NE(tip, nullptr);
        EXPECT_FALSE(tip->getTooltip().isEmpty());
    }
    EXPECT_TRUE(
        rig.map.tooltipAt(rig.map.getStripBounds(MorphMap::Axis::X).getCentre().roundToInt()).startsWith("Morph X"));
}

TEST(MorphMap, ShapeColoursComeFromTheThemeAndDifferByIndex) {
    synth::theme::Colors colors;
    EXPECT_NE(morphShapeColour(colors, 0), morphShapeColour(colors, 1));
    EXPECT_EQ(morphShapeColour(colors, 0), morphShapeColour(colors, 8));
}
