// MorphMapNamesTests.cpp -- the screen-reader names of the morph map's dots.
#include "ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"

using namespace synth::ui;
using namespace synth::ui::test;

// Regression test for FRO1297: a dot's screen-reader name said only its shape and coordinates, never which dot of how
// many it was; keyboard-reach (screen-reader half).
TEST(MorphMapNames, EveryDotIsNamedByItsPlaceInTheListItsShapeAndItsPosition) {
    ShapeRig shapes;
    MorphMap map;
    ShapeMapController controller{map, *shapes.adapter};
    map.setSize(252, 150);
    controller.refresh();
    ASSERT_EQ(map.getNumDots(), 3);
    EXPECT_EQ(map.getDotStop(0)->getTitle(), "Dot 1 of 3, shape ah, position X 20%, Y 25%");
    EXPECT_EQ(map.getDotStop(1)->getTitle(), "Dot 2 of 3, shape ee, position X 80%, Y 25%");
    EXPECT_EQ(map.getDotStop(2)->getTitle(), "Dot 3 of 3, shape oo, position X 50%, Y 80%");

    auto dots = map.getDots();
    dots.insert(dots.begin() + 1, {"mid", 0.5f, 0.25f, std::nullopt});
    map.setDots(dots);
    EXPECT_EQ(map.getDotStop(1)->getTitle(), "Dot 2 of 4, shape mid, position X 50%, Y 25%");
    EXPECT_EQ(map.getDotStop(3)->getTitle(), "Dot 4 of 4, shape oo, position X 50%, Y 80%") << "later dots renumber";

    EXPECT_TRUE(map.getDotStop(0)->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(map.getDotStop(0)->getTitle(), "Dot 1 of 4, shape ah, position X 22%, Y 25%") << "a move renames the dot";
}
