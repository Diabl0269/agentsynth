// MorphMapTests.cpp -- the morph map through real mouse and key events: cursor and dot drags, click-to-add, the dot
// menu, Delete, the cable edge bands, the tooltips, the screen-reader names and the Tab order, and the keyboard in
// the shape editor (stops that survive a refresh, the arrow-hold glide and its model).
#include "../../Accessibility/TabOrderHelpers.h"
#include "Modules/Filter/CustomFilterBlend.h"
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/MorphMap/MorphMapGlide.h"
#include "UI/ModuleViews/MorphMap/MorphMapPalette.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"
#include <cmath>

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

namespace {

/** A shape editor on a real (temporary) desktop window so keyboard focus can land on its stops. */
struct ShownEditor {
    ShapeRig rig;
    ShapeListEditor editor{rig.adapter, 0};

    ShownEditor() {
        editor.addToDesktop(juce::ComponentPeer::windowHasTitleBar);
        editor.setVisible(true);
    }
    ~ShownEditor() { editor.removeFromDesktop(); }
    bool canFocus() {
        editor.getMap().getDotStop(0)->grabKeyboardFocus();
        return editor.getMap().getDotStop(0)->hasKeyboardFocus(false);
    }
    float dotY(int i) { return rig.filter->getCustomShapes().shapes[(size_t)i].position[1]; }
    float dotX(int i) { return rig.filter->getCustomShapes().shapes[(size_t)i].position[0]; }
};

} // namespace

// Regression test for FRO946: every key step refreshed the map, which rebuilt (deleted) the focused dot stop.
TEST(MorphMapKeyboard, ARefreshKeepsTheDotStopsSoAKeyStepDoesNotDestroyTheFocusedOne) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& map = editor.getMap();
    const juce::Component::SafePointer<juce::Component> stop(map.getDotStop(1));
    const float y0 = rig.filter->getCustomShapes().shapes[1].position[1];

    map.getDotStop(1)->keyPressed(key(juce::KeyPress::downKey));
    ASSERT_NE(stop.getComponent(), nullptr) << "the stop that took the key is still there after the refresh";
    EXPECT_EQ(map.getDotStop(1), stop.getComponent());
    stop->keyPressed(key(juce::KeyPress::downKey));

    const auto& shapes = rig.filter->getCustomShapes().shapes;
    EXPECT_NEAR(shapes[1].position[1], y0 + 2 * MorphMap::kStep, 1e-4);
    EXPECT_NEAR(shapes[0].position[1], 0.25f, 1e-5) << "no other dot moved";
    EXPECT_NEAR(shapes[2].position[1], 0.8f, 1e-5);
}

TEST(MorphMapKeyboard, AllFourArrowsMoveTheSameDotAcrossRefreshes) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& map = editor.getMap();
    const auto before = rig.filter->getCustomShapes().shapes[1].position;

    map.getDotStop(1)->keyPressed(key(juce::KeyPress::leftKey));
    map.getDotStop(1)->keyPressed(key(juce::KeyPress::upKey));
    map.getDotStop(1)->keyPressed(key(juce::KeyPress::rightKey));
    map.getDotStop(1)->keyPressed(key(juce::KeyPress::rightKey));
    map.getDotStop(1)->keyPressed(key(juce::KeyPress::downKey));
    map.getDotStop(1)->keyPressed(key(juce::KeyPress::downKey));

    const auto& position = rig.filter->getCustomShapes().shapes[1].position;
    EXPECT_NEAR(position[0], before[0] + MorphMap::kStep, 1e-4);
    EXPECT_NEAR(position[1], before[1] + MorphMap::kStep, 1e-4);
}

TEST(MorphMapKeyboard, AStopSurvivesARefreshWithTheSameCountAndTheNamesFollowTheDots) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    auto& map = editor.getMap();
    const juce::Component::SafePointer<juce::Component> first(map.getDotStop(0));
    const juce::Component::SafePointer<juce::Component> last(map.getDotStop(2));

    editor.getController().renameShape(0, "renamed");
    EXPECT_EQ(map.getDotStop(0), first.getComponent());
    ASSERT_NE(first.getComponent(), nullptr);
    ASSERT_NE(last.getComponent(), nullptr);
    EXPECT_EQ(map.getDotStop(2), last.getComponent());
    EXPECT_TRUE(map.getDotStop(0)->getTitle().startsWith("renamed"));

    editor.getController().addShapeInFreeSpot();
    EXPECT_EQ(map.getDotStop(0), first.getComponent()) << "a new dot adds a stop; the others stay";
    EXPECT_EQ(map.getDotStop(2), last.getComponent());
    EXPECT_NE(map.getDotStop(3), nullptr);
    EXPECT_EQ(map.getDotStop(3)->getExplicitFocusOrder(), 4);
}

TEST(MorphMapKeyboard, FocusOnAStopSurvivesAKeyStep) {
    ShownEditor shown;
    if (!shown.canFocus())
        GTEST_SKIP() << "no keyboard focus in this environment";
    auto& map = shown.editor.getMap();
    map.getDotStop(1)->grabKeyboardFocus();
    const float y0 = shown.dotY(1);

    for (int i = 0; i < 2; ++i) {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        ASSERT_EQ(focused, map.getDotStop(1));
        focused->keyPressed(key(juce::KeyPress::downKey));
    }

    EXPECT_NEAR(shown.dotY(1), y0 + 2 * MorphMap::kStep, 1e-4);
    EXPECT_TRUE(map.getDotStop(1)->hasKeyboardFocus(false));
}

// Regression test for FRO949: Delete on a dot left nothing focused, so the next Tab started from the top.
TEST(MorphMapKeyboard, DeleteFocusesTheNextDotAndTheNewLastWhenTheLastWasDeleted) {
    ShownEditor shown;
    if (!shown.canFocus())
        GTEST_SKIP() << "no keyboard focus in this environment";
    auto& map = shown.editor.getMap();
    map.grabKeyboardFocus();

    map.getDotStop(1)->keyPressed(key(juce::KeyPress::backspaceKey));
    ASSERT_EQ(map.getNumDots(), 2);
    EXPECT_EQ(map.getDots()[1].name, "oo");
    EXPECT_TRUE(map.getDotStop(1)->hasKeyboardFocus(false)) << "the former dot 2 is now dot 2 of 2 and has focus";

    map.grabKeyboardFocus();
    map.getDotStop(1)->keyPressed(key(juce::KeyPress::deleteKey));
    ASSERT_EQ(map.getNumDots(), 1);
    EXPECT_TRUE(map.getDotStop(0)->hasKeyboardFocus(false)) << "deleting the last dot focuses the new last";
}

// Regression test for FRO945: a freshly opened shape window had nothing focused, so Tab did nothing.
TEST(MorphMapKeyboard, FocusFirstControlPutsTheKeyboardOnTheMapSoTabStartsThere) {
    ShownEditor shown;
    if (!shown.canFocus())
        GTEST_SKIP() << "no keyboard focus in this environment";
    shown.editor.getDoneButton().grabKeyboardFocus();
    ASSERT_FALSE(shown.editor.getMap().hasKeyboardFocus(true));

    shown.editor.focusFirstControl();

    EXPECT_TRUE(shown.editor.getMap().hasKeyboardFocus(false));
    EXPECT_EQ(juce::Component::getCurrentlyFocusedComponent(), &shown.editor.getMap());
}

TEST(MorphMapKeyboard, FocusFirstControlDoesNothingWhileTheEditorIsNotOnScreen) {
    ShapeRig rig;
    ShapeListEditor editor(rig.adapter, 0);
    editor.focusFirstControl();
    EXPECT_FALSE(editor.getMap().hasKeyboardFocus(true));
}

// ---------------------------------------------------------------------------------------------------------------
// The glide model
// ---------------------------------------------------------------------------------------------------------------

TEST(MorphMapGlideModel, ATapDoesNotMoveAnythingByItself) {
    MorphMapGlide glide;
    ASSERT_TRUE(glide.press(1000.0, {0.5, 0.5}));
    EXPECT_FALSE(glide.isGliding(1000.0 + morph_glide::kTapMs - 1.0));
    const auto p = glide.advance(1000.0 + morph_glide::kTapMs - 1.0, 1, 0);
    EXPECT_DOUBLE_EQ(p.x, 0.5);
    EXPECT_DOUBLE_EQ(p.y, 0.5);
    EXPECT_EQ(glide.velocityAt(1000.0), 0.0);
}

TEST(MorphMapGlideModel, AHeldPositionFollowsTheRampIntegral) {
    MorphMapGlide glide;
    glide.press(0.0, {0.0, 0.5});
    const double start = morph_glide::kTapMs;
    EXPECT_NEAR(glide.velocityAt(start), morph_glide::kStartUnitsPerSecond, 1e-9);
    EXPECT_NEAR(glide.velocityAt(start + morph_glide::kRampMs), morph_glide::kMaxUnitsPerSecond, 1e-9);

    // One second in: v0 * t + rise * ramp * (t / ramp)^4 / 4.
    const double rise = morph_glide::kMaxUnitsPerSecond - morph_glide::kStartUnitsPerSecond;
    const double expected = 0.15 * 1.0 + rise * 2.0 * std::pow(0.5, 4.0) / 4.0;
    EXPECT_NEAR(glide.advance(start + 1000.0, 1, 0).x, expected, 1e-9);
    // Two steps cover the same distance as one, whatever the frame rate.
    MorphMapGlide split;
    split.press(0.0, {0.0, 0.5});
    split.advance(start + 400.0, 1, 0);
    EXPECT_NEAR(split.advance(start + 1000.0, 1, 0).x, expected, 1e-9);
    // Past the ramp it moves at the cap.
    MorphMapGlide capped;
    capped.press(0.0, {0.0, 0.5});
    capped.advance(start + 5000.0, 0, 0); // the distance so far is banked without moving anything
    EXPECT_NEAR(capped.advance(start + 5500.0, 1, 0).x, morph_glide::kMaxUnitsPerSecond * 0.5, 1e-9);
}

TEST(MorphMapGlideModel, ShiftScalesTheSpeedAndADiagonalMovesBothAxesAtTheSameSpeedAsAStraightLine) {
    MorphMapGlide straight, shifted, diagonal;
    straight.press(0.0, {0.0, 0.0});
    shifted.press(0.0, {0.0, 0.0});
    diagonal.press(0.0, {0.0, 0.0});
    const double t = morph_glide::kTapMs + 800.0;
    const double s = straight.advance(t, 1, 0).x;
    EXPECT_NEAR(shifted.advance(t, 1, 0, morph_glide::kShiftSpeedFactor).x, s * morph_glide::kShiftSpeedFactor, 1e-9);
    const auto d = diagonal.advance(t, 1, 1);
    EXPECT_NEAR(d.x, d.y, 1e-12);
    EXPECT_NEAR(std::hypot(d.x, d.y), s, 1e-9);
}

TEST(MorphMapGlideModel, ClampsToTheMapAndReleaseStopsIt) {
    MorphMapGlide glide;
    glide.press(0.0, {0.9, 0.1});
    const double t = morph_glide::kTapMs + 10000.0;
    const auto p = glide.advance(t, 1, -1);
    EXPECT_DOUBLE_EQ(p.x, 1.0);
    EXPECT_DOUBLE_EQ(p.y, 0.0);
    EXPECT_FALSE(glide.press(t, {0.0, 0.0})) << "a repeat keeps the running hold";
    const auto end = glide.release();
    EXPECT_FALSE(glide.isHeld());
    EXPECT_DOUBLE_EQ(end.x, 1.0);
    EXPECT_DOUBLE_EQ(glide.advance(t + 1000.0, 1, 0).x, 1.0) << "nothing moves after release";
    EXPECT_TRUE(glide.press(t, {0.2, 0.2})) << "and a new press starts a new hold";
}

// ---------------------------------------------------------------------------------------------------------------
// The map driving it
// ---------------------------------------------------------------------------------------------------------------

namespace {
struct HeldKeys {
    double now = 0.0;
    bool left = false, right = false, up = false, down = false, shift = false;

    MorphMap::GlideHost host() {
        MorphMap::GlideHost h;
        h.nowMs = [this] { return now; };
        h.isArrowDown = [this](int code) {
            return (code == juce::KeyPress::leftKey && left) || (code == juce::KeyPress::rightKey && right) ||
                   (code == juce::KeyPress::upKey && up) || (code == juce::KeyPress::downKey && down);
        };
        h.isShiftDown = [this] { return shift; };
        return h;
    }
};

struct GlideRig {
    ShapeRig rig;
    ShapeListEditor editor{rig.adapter, 0};
    HeldKeys keys;
    int starts = 0;
    int ends = 0;

    GlideRig() {
        auto& map = editor.getMap();
        map.setGlideHostForTest(keys.host());
        const auto controllerStart = map.onGestureStart;
        const auto controllerEnd = map.onGestureEnd;
        map.onGestureStart = [this, controllerStart](MorphMap::Gesture g) {
            ++starts;
            controllerStart(g);
        };
        map.onGestureEnd = [this, controllerEnd](MorphMap::Gesture g) {
            ++ends;
            controllerEnd(g);
        };
    }
    float y(int i) { return rig.filter->getCustomShapes().shapes[(size_t)i].position[1]; }
    float x(int i) { return rig.filter->getCustomShapes().shapes[(size_t)i].position[0]; }
};
} // namespace

TEST(MorphMapGlide, ATapIsOneStepAndOneUndoStep) {
    GlideRig g;
    auto& map = g.editor.getMap();
    const float y0 = g.y(1);
    const int serial = g.rig.undo.getEditSerial();

    g.keys.down = true;
    map.getDotStop(1)->keyPressed(key(juce::KeyPress::downKey));
    g.keys.now = 50.0;
    g.keys.down = false;
    map.keyStateChanged(false);

    EXPECT_NEAR(g.y(1), y0 + MorphMap::kStep, 1e-4);
    EXPECT_EQ(g.starts, 1);
    EXPECT_EQ(g.ends, 1);
    EXPECT_EQ(g.rig.undo.getEditSerial(), serial + 1);
    EXPECT_FALSE(map.isGliding());
}

TEST(MorphMapGlide, HoldingAnArrowGlidesTheDotInOneGestureAndOneUndoStep) {
    GlideRig g;
    auto& map = g.editor.getMap();
    const float y0 = g.y(1);
    const int serial = g.rig.undo.getEditSerial();

    g.keys.down = true;
    map.getDotStop(1)->keyPressed(key(juce::KeyPress::downKey));
    map.getDotStop(1)->keyPressed(key(juce::KeyPress::downKey)); // OS auto-repeat
    EXPECT_TRUE(map.isGliding());
    g.keys.now = morph_glide::kTapMs + 1000.0;
    map.glideTickForTest();
    const float held = g.y(1);
    EXPECT_GT(held, y0 + MorphMap::kStep + 0.1f) << "it keeps going past the first step";
    EXPECT_EQ(g.ends, 0) << "the gesture stays open for the whole hold";

    g.keys.now = morph_glide::kTapMs + 3000.0;
    map.glideTickForTest();
    EXPECT_GT(g.y(1), held);
    g.keys.down = false;
    map.keyStateChanged(false);

    EXPECT_EQ(g.starts, 1);
    EXPECT_EQ(g.ends, 1);
    EXPECT_EQ(g.rig.undo.getEditSerial(), serial + 1);
    EXPECT_FALSE(map.isGliding());
    const float rest = g.y(1);
    g.keys.now += 1000.0;
    map.glideTickForTest();
    EXPECT_FLOAT_EQ(g.y(1), rest) << "released: nothing moves any more";
    g.rig.undo.undo();
    EXPECT_NEAR(g.y(1), y0, 1e-4);
}

TEST(MorphMapGlide, TwoArrowsMoveDiagonallyShiftIsFasterAndTheMapEdgeClamps) {
    GlideRig g;
    auto& map = g.editor.getMap();
    const float x0 = g.x(1), y0 = g.y(1);

    g.keys.down = g.keys.right = true;
    map.getDotStop(1)->keyPressed(key(juce::KeyPress::downKey));
    g.keys.now = morph_glide::kTapMs + 500.0;
    map.glideTickForTest();
    const float dx = g.x(1) - x0, dy = g.y(1) - y0;
    EXPECT_GT(dx, 0.0f);
    EXPECT_GT(dy, MorphMap::kStep) << "down moved at the press, then both glide";

    g.keys.shift = true;
    g.keys.now += 500.0;
    map.glideTickForTest();
    EXPECT_GT(g.x(1) - x0 - dx, dx * 1.5f) << "Shift covers more in the same time";

    g.keys.now += 60000.0;
    map.glideTickForTest();
    EXPECT_FLOAT_EQ(g.x(1), 1.0f);
    EXPECT_FLOAT_EQ(g.y(1), 1.0f);
    g.keys.down = g.keys.right = false;
    map.keyStateChanged(false);
}

TEST(MorphMapGlide, AStripGlidesOnlyAlongItsAxisAndTheCursorMovesLikeADot) {
    GlideRig g;
    auto& map = g.editor.getMap();
    const float cy = g.rig.filter->getCurrentMorphY();

    g.keys.right = true;
    map.getStripStop(MorphMap::Axis::X)->keyPressed(key(juce::KeyPress::rightKey));
    g.keys.now = morph_glide::kTapMs + 800.0;
    map.glideTickForTest();
    EXPECT_GT(g.rig.filter->getCurrentMorphX(), 0.5f + MorphMap::kStep + 0.05f);
    EXPECT_FLOAT_EQ(g.rig.filter->getCurrentMorphY(), cy) << "the X slider never moves the other coordinate";
    g.keys.right = false;
    map.keyStateChanged(false);

    g.keys.now = 0.0;
    g.keys.up = true;
    map.getCursorStop()->keyPressed(key(juce::KeyPress::upKey));
    g.keys.now = morph_glide::kTapMs + 800.0;
    map.glideTickForTest();
    EXPECT_LT(g.rig.filter->getCurrentMorphY(), cy - MorphMap::kStep);
    g.keys.up = false;
    map.keyStateChanged(false);
    EXPECT_EQ(g.starts, g.ends);
}

TEST(MorphMapGlide, ALostKeyUpIsSeenByTheFramePoll) {
    GlideRig g;
    auto& map = g.editor.getMap();
    g.keys.down = true;
    map.getDotStop(1)->keyPressed(key(juce::KeyPress::downKey));
    g.keys.down = false; // the key-up never reached the map
    g.keys.now = morph_glide::kTapMs + 500.0;
    const float before = g.y(1);
    map.glideTickForTest();
    EXPECT_FLOAT_EQ(g.y(1), before) << "no arrow is down, so the frame does not move it";
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_FALSE(map.isGliding());
    EXPECT_EQ(g.ends, 1);
}
