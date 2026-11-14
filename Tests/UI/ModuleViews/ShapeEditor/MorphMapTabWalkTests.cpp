// MorphMapTabWalkTests.cpp -- Tab and Shift+Tab on a focused morph map stop walk the dots by index, then the cursor,
// then the Morph X and Morph Y sliders; past either end the key goes on to the region cycle. Keys are delivered to the
// focused stop's own keyPressed, as the window's peer does, and the stop the walk asks for is read from
// getLastWalkTargetForTest (a headless test has no native window to hold focus).
#include "ShapeEditorTestHelpers.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

const juce::KeyPress kTab(juce::KeyPress::tabKey, juce::ModifierKeys::noModifiers, 0);
const juce::KeyPress kShiftTab(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0);

struct WalkRig : ShapeRig {
    MorphMap map;
    ShapeMapController controller{map, *adapter};

    WalkRig() {
        map.setSize(252, 150);
        controller.refresh();
    }
    // The stops in the order the walk must visit them.
    std::vector<juce::Component*> order() {
        std::vector<juce::Component*> stops;
        for (int i = 0; i < map.getNumDots(); ++i)
            stops.push_back(map.getDotStop(i));
        stops.push_back(map.getCursorStop());
        stops.push_back(map.getStripStop(MorphMap::Axis::X));
        stops.push_back(map.getStripStop(MorphMap::Axis::Y));
        return stops;
    }
};

} // namespace

// Regression test for FRO1582: Down or Right picked the first dot and nothing stepped to the second, so only dot 1
// could ever be edited with the keyboard; keyboard-reach.
TEST(MorphMapTabWalk, TabStepsFromDotToDotThenTheCursorThenTheSlidersAndLeavesPastTheEnd) {
    WalkRig rig;
    const auto stops = rig.order();
    ASSERT_EQ(stops.size(), 6u);
    for (size_t i = 0; i + 1 < stops.size(); ++i) {
        SCOPED_TRACE(stops[i]->getTitle().toStdString());
        EXPECT_TRUE(stops[i]->keyPressed(kTab)) << "Tab is claimed";
        EXPECT_EQ(rig.map.getLastWalkTargetForTest(), stops[i + 1]);
    }
    EXPECT_FALSE(stops.back()->keyPressed(kTab)) << "past the last slider Tab leaves through the region cycle";
}

TEST(MorphMapTabWalk, ShiftTabWalksBackAndLeavesBeforeTheFirstDot) {
    WalkRig rig;
    const auto stops = rig.order();
    for (size_t i = stops.size() - 1; i > 0; --i) {
        SCOPED_TRACE(stops[i]->getTitle().toStdString());
        EXPECT_TRUE(stops[i]->keyPressed(kShiftTab));
        EXPECT_EQ(rig.map.getLastWalkTargetForTest(), stops[i - 1]);
    }
    EXPECT_FALSE(stops.front()->keyPressed(kShiftTab)) << "before the first dot Shift+Tab leaves";
}

TEST(MorphMapTabWalk, TheDotTabLandsOnIsNamedWithItsPlaceInTheList) {
    WalkRig rig;
    ASSERT_TRUE(rig.map.getDotStop(0)->keyPressed(kTab));
    ASSERT_EQ(rig.map.getLastWalkTargetForTest(), rig.map.getDotStop(1));
    EXPECT_TRUE(rig.map.getLastWalkTargetForTest()->getTitle().startsWith("Dot 2 of 3, "))
        << rig.map.getLastWalkTargetForTest()->getTitle();
}

TEST(MorphMapTabWalk, ArrowsStillMoveTheFocusedDotAndReturnStillOpensItsMenu) {
    WalkRig rig;
    int menus = 0;
    rig.map.showMenu = [&menus](juce::PopupMenu&, juce::Rectangle<int>) { ++menus; };
    const auto before = rig.map.getDots()[1];
    EXPECT_TRUE(rig.map.getDotStop(1)->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_GT(rig.map.getDots()[1].x, before.x) << "the arrow moves the dot; it is not a step to another stop";
    EXPECT_EQ(rig.map.getLastWalkTargetForTest(), nullptr);
    EXPECT_TRUE(rig.map.getDotStop(1)->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(menus, 1);
}

TEST(MorphMapTabWalk, AHiddenCursorTakesItsSlidersOutOfTheWalk) {
    WalkRig rig;
    rig.map.setCursorVisible(false);
    auto* lastDot = rig.map.getDotStop(rig.map.getNumDots() - 1);
    EXPECT_FALSE(lastDot->keyPressed(kTab)) << "no cursor and no sliders to step to: Tab leaves";
}

TEST(MorphMapTabWalk, TheWalkFollowsTheRegionCycleBindings) {
    WalkRig rig;
    ShortcutManager manager;
    rig.map.setShortcuts([&manager] { return &manager; });
    const juce::KeyPress f6(juce::KeyPress::F6Key, juce::ModifierKeys::noModifiers, 0);
    manager.setBinding("focusNextRegion", f6);
    EXPECT_FALSE(rig.map.getDotStop(0)->keyPressed(kTab)) << "Tab is no longer the cycle key, so it is not a walk key";
    EXPECT_TRUE(rig.map.getDotStop(0)->keyPressed(f6));
    EXPECT_EQ(rig.map.getLastWalkTargetForTest(), rig.map.getDotStop(1));
}
