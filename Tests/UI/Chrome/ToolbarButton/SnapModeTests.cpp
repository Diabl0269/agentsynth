// SnapModeTests.cpp -- the Snap mode picker's pure half (which directions each mode snaps, the saved key, the grid a
// Filter choice draws per mode) and the picker button itself: its menu, remembered mode and keyboard path.
#include "../../../TestSettingsHelpers.h"
#include "../../../TestWait.h"
#include "UI/Chrome/ToolbarButton/SnapModeButton.h"
#include "UI/ModuleViews/ShapeEditor/PitchGrid.h"
#include <gtest/gtest.h>

using namespace synth::ui;

TEST(SnapMode, EachModeSaysWhichDirectionsItSnaps) {
    EXPECT_TRUE(snapsTime(SnapMode::TimeAndLevel) && snapsLevel(SnapMode::TimeAndLevel));
    EXPECT_TRUE(snapsTime(SnapMode::TimeOnly));
    EXPECT_FALSE(snapsLevel(SnapMode::TimeOnly));
    EXPECT_FALSE(snapsTime(SnapMode::LevelOnly));
    EXPECT_TRUE(snapsLevel(SnapMode::LevelOnly));
}

TEST(SnapMode, KeysRoundTripAndAnUnknownOrMissingOneReadsTimeAndLevel) {
    for (const auto mode : kSnapModes)
        EXPECT_EQ(snapModeFromKey(snapModeKey(mode)), mode);
    EXPECT_EQ(snapModeFromKey({}), SnapMode::TimeAndLevel);
    EXPECT_EQ(snapModeFromKey("sideways"), SnapMode::TimeAndLevel);
    EXPECT_EQ(snapModeName(SnapMode::TimeAndLevel), "Time and level");
    EXPECT_EQ(snapModeName(SnapMode::TimeOnly), "Time only");
    EXPECT_EQ(snapModeName(SnapMode::LevelOnly), "Level only");
}

TEST(SnapMode, TheFilterGridSnapsEachAxisAsTheModeAllowsAndNeitherWithSnapOff) {
    pitchgrid::Choice choice;
    choice.kind = pitchgrid::Kind::octaves;
    choice.levelDb = 6.0f;
    for (const bool snap : {true, false})
        for (const auto mode : kSnapModes) {
            choice.snap = snap;
            choice.snapMode = mode;
            const auto grid = pitchgrid::gridFor(choice, {});
            ASSERT_TRUE(grid.has_value());
            EXPECT_EQ(grid->snapX, snap && snapsTime(mode)) << snapModeKey(mode) << " snap=" << snap;
            EXPECT_EQ(grid->snapY, snap && snapsLevel(mode)) << snapModeKey(mode) << " snap=" << snap;
        }
    choice.snapMode = SnapMode::TimeAndLevel;
    pitchgrid::Choice other = choice;
    other.snapMode = SnapMode::LevelOnly;
    EXPECT_FALSE(choice == other) << "the mode is part of the choice";
}

TEST(SnapModeButton, TheMenuListsThreeModesWithTheCurrentOneChecked) {
    SnapModeButton button("testSnapMode", "testSnapModeKey");
    juce::PopupMenu captured;
    button.showMenuHook = [&captured](juce::PopupMenu& menu) { captured = menu; };
    button.showMenu();
    std::vector<std::pair<juce::String, bool>> rows;
    for (juce::PopupMenu::MenuItemIterator it(captured); it.next();)
        rows.emplace_back(it.getItem().text, it.getItem().isTicked);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0], (std::pair<juce::String, bool>{"Time and level", true}));
    EXPECT_EQ(rows[1], (std::pair<juce::String, bool>{"Time only", false}));
    EXPECT_EQ(rows[2], (std::pair<juce::String, bool>{"Level only", false}));

    button.choose(SnapMode::TimeOnly);
    EXPECT_EQ(button.getIcon(), synth::theme::Icon::SnapModeTime);
    EXPECT_EQ(button.getTitle(), "Snap mode");
    EXPECT_EQ(button.getDescription(), "Time only");
    EXPECT_FALSE(button.getToggleState()) << "never lit";
    button.showMenu();
    int ticked = -1;
    int index = 0;
    for (juce::PopupMenu::MenuItemIterator it(captured); it.next(); ++index)
        if (it.getItem().isTicked)
            ticked = index;
    EXPECT_EQ(ticked, 1);
}

TEST(SnapModeButton, ItIsATabStopThatOpensItsMenuOnReturnButAClickNeverFocusesIt) {
    SnapModeButton button("testSnapMode", "testSnapModeKey");
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    EXPECT_FALSE(button.getMouseClickGrabsKeyboardFocus());
    int opened = 0;
    button.showMenuHook = [&opened](juce::PopupMenu&) { ++opened; };
    EXPECT_TRUE(static_cast<juce::Component&>(button).keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    synth::test::flushMessageQueue();
    EXPECT_EQ(opened, 1);
}

TEST(SnapModeButton, TheChosenModeIsSavedAndRestoredAndAMissingOneReadsTimeAndLevel) {
    synth::test::PersistedKeysGuard guard{juce::StringArray{"testSnapModeKey"}};
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    props.getUserSettings()->removeValue("testSnapModeKey");
    {
        SnapModeButton button("testSnapMode", "testSnapModeKey");
        std::optional<SnapMode> announced;
        button.onModeChanged = [&announced](SnapMode mode) { announced = mode; };
        button.setApplicationProperties(&props);
        EXPECT_EQ(button.getMode(), SnapMode::TimeAndLevel);
        button.choose(SnapMode::LevelOnly);
        EXPECT_EQ(announced, SnapMode::LevelOnly);
    }
    SnapModeButton again("testSnapMode", "testSnapModeKey");
    again.setApplicationProperties(&props);
    EXPECT_EQ(again.getMode(), SnapMode::LevelOnly);
    EXPECT_EQ(again.getIcon(), synth::theme::Icon::SnapModeLevel);
}
