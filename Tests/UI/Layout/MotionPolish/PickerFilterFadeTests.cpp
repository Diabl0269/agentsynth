#include "MotionStep.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Timeline/MidiDestinationPicker.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Topic: the Mod Matrix picker and the MIDI destination picker fade the rows their search filter takes out or brings
// back while the list closes up (docs/layout/animation.md#fading-things-in-and-out). Headless: the animated path is
// forced with FadeAnimateGuard and stepped by hand.

namespace {
using synth::ui::AnimationMode;
using synth::ui::MidiDestinationPicker;
using synth::ui::ModMatrixPicker;

std::vector<ModMatrixPicker::Item> matrixItems() {
    return {{.id = 1, .category = "Oscillators", .text = "Alpha One"},
            {.id = 2, .category = "Oscillators", .text = "Alpha Two"},
            {.id = 3, .category = "Filters", .text = "Beta One"}};
}

int shownChildren(juce::Component& column) {
    int count = 0;
    for (auto* child : column.getChildren())
        count += child->isVisible() ? 1 : 0;
    return count;
}

// The alpha of the first child that is part way through a fade, or -1 when none is.
float midFadeAlpha(juce::Component& column) {
    for (auto* child : column.getChildren())
        if (child->isVisible() && child->getAlpha() > 0.0f && child->getAlpha() < 1.0f)
            return child->getAlpha();
    return -1.0f;
}

std::vector<MidiDestinationPicker::Option> midiOptions() {
    return {{.displayName = "Synth A", .nodeUid = 11, .connected = false},
            {.displayName = "Synth B", .nodeUid = 12, .connected = false},
            {.displayName = "Gate", .nodeUid = 13, .connected = false}};
}
} // namespace

TEST(PickerFilterFade, ModMatrixRowsTheFilterRemovesFadeWhileTheListClosesUp) {
    FadeAnimateGuard guard;
    int picked = 0;
    ModMatrixPicker picker("source", matrixItems(), 0, [&](int id) { picked = id; });
    auto& column = picker.getRowColumnForTest();
    const int full = column.getHeight();
    ASSERT_EQ(shownChildren(column), 5) << "two headers and three rows";

    picker.setSearchTextForTest("beta");
    EXPECT_EQ(picker.getVisibleItemTextsForTest(), std::vector<juce::String>{"Beta One"}) << "logically gone at once";
    EXPECT_TRUE(picker.isFilterFadingForTest());
    EXPECT_EQ(shownChildren(column), 5) << "the leaving rows stay on screen while they fade";
    EXPECT_EQ(column.getHeight(), full) << "nothing has moved at frame 0";

    stepMotion(0.5f);
    EXPECT_GT(midFadeAlpha(column), 0.0f);
    EXPECT_LT(column.getHeight(), full) << "the list closes up with the fade";
    const int mid = column.getHeight();

    picker.chooseVisibleItemForTest(0);
    EXPECT_EQ(picked, 3) << "a pick lands on a row that is still shown, never on one that is leaving";

    stepMotion(1.0f);
    EXPECT_EQ(shownChildren(column), 2) << "Beta's header and row";
    EXPECT_LT(column.getHeight(), mid);
    EXPECT_FALSE(picker.isFilterFadingForTest());
    EXPECT_FLOAT_EQ(midFadeAlpha(column), -1.0f);
}

TEST(PickerFilterFade, ModMatrixTypingRetargetsFromTheCurrentOpacityWithoutWaiting) {
    FadeAnimateGuard guard;
    ModMatrixPicker picker("source", matrixItems(), 0, [](int) {});
    auto& column = picker.getRowColumnForTest();

    picker.setSearchTextForTest("beta");
    stepMotion(0.5f);
    const float mid = midFadeAlpha(column);
    ASSERT_GT(mid, 0.0f);

    picker.setSearchTextForTest(""); // the next keystroke brings the rows back
    EXPECT_EQ(picker.getVisibleItemTextsForTest().size(), 3u);
    EXPECT_NEAR(midFadeAlpha(column), mid, 0.01f) << "the reversal continues from where the fade is";
    stepMotion(1.0f);
    EXPECT_EQ(shownChildren(column), 5);
    EXPECT_FLOAT_EQ(midFadeAlpha(column), -1.0f);
}

TEST(PickerFilterFade, ModMatrixReduceMotionIsAShortFadeAndOffAndOffScreenAreInstant) {
    {
        FadeAnimateGuard reduced(AnimationMode::reduced);
        ModMatrixPicker picker("source", matrixItems(), 0, [](int) {});
        picker.setSearchTextForTest("beta");
        EXPECT_TRUE(picker.isFilterFadingForTest());
        EXPECT_EQ(shownChildren(picker.getRowColumnForTest()), 5);
        stepMotion(1.0f);
        EXPECT_EQ(shownChildren(picker.getRowColumnForTest()), 2);
    }
    {
        FadeAnimateGuard off(AnimationMode::off);
        ModMatrixPicker picker("source", matrixItems(), 0, [](int) {});
        picker.setSearchTextForTest("beta");
        EXPECT_FALSE(picker.isFilterFadingForTest());
        EXPECT_EQ(shownChildren(picker.getRowColumnForTest()), 2);
    }
    ModMatrixPicker picker("source", matrixItems(), 0, [](int) {}); // not on screen
    picker.setSearchTextForTest("beta");
    EXPECT_FALSE(picker.isFilterFadingForTest());
    EXPECT_EQ(shownChildren(picker.getRowColumnForTest()), 2);
}

TEST(PickerFilterFade, MidiDestinationRowsTheSearchRemovesFadeWhileTheListClosesUp) {
    FadeAnimateGuard guard;
    MidiDestinationPicker picker([] { return midiOptions(); }, [](juce::uint32, bool) {});
    auto& column = picker.getRowColumnForTest();
    const int full = column.getHeight();
    ASSERT_EQ(shownChildren(column), 3);

    picker.setSearchTextForTest("gate");
    EXPECT_EQ(picker.getVisibleRowNamesForTest(), std::vector<juce::String>{"Gate"}) << "logically gone at once";
    EXPECT_EQ(shownChildren(column), 3) << "the leaving rows stay on screen while they fade";
    EXPECT_TRUE(picker.isFilterFadingForTest());
    EXPECT_EQ(column.getHeight(), full);

    stepMotion(0.5f);
    EXPECT_GT(midFadeAlpha(column), 0.0f);
    EXPECT_LT(column.getHeight(), full);
    const float mid = midFadeAlpha(column);

    picker.setSearchTextForTest(""); // typing on: back from the current opacity
    EXPECT_NEAR(midFadeAlpha(column), mid, 0.01f);
    stepMotion(1.0f);
    EXPECT_EQ(shownChildren(column), 3);

    picker.setSearchTextForTest("gate");
    stepMotion(1.0f);
    EXPECT_EQ(shownChildren(column), 1);
    EXPECT_FALSE(picker.isFilterFadingForTest());
}

TEST(PickerFilterFade, MidiDestinationTogglesTheShownRowNotOneThatIsLeaving) {
    FadeAnimateGuard guard;
    std::vector<MidiDestinationPicker::Option> options = midiOptions();
    juce::uint32 toggled = 0;
    MidiDestinationPicker picker([&] { return options; }, [&](juce::uint32 uid, bool) { toggled = uid; });
    picker.setSearchTextForTest("gate");
    picker.toggleRowForTest(0);
    EXPECT_EQ(toggled, 13u);
}

TEST(PickerFilterFade, MidiDestinationReduceMotionFadesAndOffAndOffScreenAreInstant) {
    {
        FadeAnimateGuard reduced(AnimationMode::reduced);
        MidiDestinationPicker picker([] { return midiOptions(); }, [](juce::uint32, bool) {});
        picker.setSearchTextForTest("gate");
        EXPECT_TRUE(picker.isFilterFadingForTest());
        stepMotion(1.0f);
        EXPECT_EQ(shownChildren(picker.getRowColumnForTest()), 1);
    }
    {
        FadeAnimateGuard off(AnimationMode::off);
        MidiDestinationPicker picker([] { return midiOptions(); }, [](juce::uint32, bool) {});
        picker.setSearchTextForTest("gate");
        EXPECT_FALSE(picker.isFilterFadingForTest());
        EXPECT_EQ(shownChildren(picker.getRowColumnForTest()), 1);
    }
    MidiDestinationPicker picker([] { return midiOptions(); }, [](juce::uint32, bool) {});
    picker.setSearchTextForTest("gate");
    EXPECT_FALSE(picker.isFilterFadingForTest());
    EXPECT_EQ(shownChildren(picker.getRowColumnForTest()), 1);
}
