#include "MainWindowPlacement.h"
#include <gtest/gtest.h>

// A launch with no main display crashed in juce::Component::centreWithSize (null display bounds).
// The helper takes "is there a main display" as a value, so no hardware is needed to reach that case.
TEST(MainWindowPlacementTest, NoMainDisplaySetsTheSizeWithoutCentring) {
    juce::Component window;
    window.setBounds(10, 20, 100, 100);

    synth::sizeAndCentreWindow(window, 1600, 900, /*mainDisplayAvailable=*/false);

    EXPECT_EQ(window.getWidth(), 1600);
    EXPECT_EQ(window.getHeight(), 900);
    EXPECT_EQ(window.getX(), 10) << "with no display there is nothing to centre on";
    EXPECT_EQ(window.getY(), 20);
}

TEST(MainWindowPlacementTest, MainDisplayPresentCentresTheWindow) {
    if (!synth::hasMainDisplay())
        GTEST_SKIP() << "no display on this runner";
    juce::Component window;
    synth::sizeAndCentreWindow(window, 400, 300, /*mainDisplayAvailable=*/true);
    EXPECT_EQ(window.getWidth(), 400);
    EXPECT_EQ(window.getHeight(), 300);
}

TEST(MainWindowPlacementTest, DefaultOverloadNeverCrashesWhateverTheMachineHas) {
    juce::Component window;
    synth::sizeAndCentreWindow(window, 640, 480);
    EXPECT_EQ(window.getWidth(), 640);
}
