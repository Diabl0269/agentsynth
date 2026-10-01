// StatusBarAccessibilityTests.cpp -- the status bar's spoken text (docs/layout/chrome.md#status-bar): the
// one value a screen reader gets for the painted items, in the order they are drawn.
#include "UI/Chrome/StatusBarComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

class StatusBarAccessibilityTest : public ::testing::Test {
protected:
    void feed(int width) {
        bar_.setSize(width, 24);
        bar_.update(13.0f, 0, "Project clips");
        bar_.updateRoundTripLatency(12.5, true);
        bar_.updateTransport(false, "001.1.000", 120.0);
    }
    StatusBarComponent bar_;
};

} // namespace

TEST_F(StatusBarAccessibilityTest, ListsEveryVisibleItemInDrawnOrder) {
    feed(1000);
    EXPECT_EQ(bar_.getAccessibilityText(), "Project clips, CPU 13%, RT 12.5 ms, position 001.1.000, 120 BPM, 0 voices");
}

TEST_F(StatusBarAccessibilityTest, LeavesOutWhatTheBarDropsWhenItIsTooNarrowToDrawIt) {
    feed(400);
    EXPECT_EQ(bar_.getAccessibilityText(), "Project clips, CPU 13%, 0 voices");
}

TEST_F(StatusBarAccessibilityTest, SpeaksFractionalTempoVoiceCountAndUnavailableLatency) {
    feed(1000);
    bar_.update(80.4f, 1, "Lead");
    bar_.updateRoundTripLatency(0.0, false);
    bar_.updateTransport(true, "002.3.480", 98.5);
    EXPECT_EQ(bar_.getAccessibilityText(), "Lead, CPU 80%, RT unavailable, position 002.3.480, 98.5 BPM, 1 voice");
}

TEST_F(StatusBarAccessibilityTest, ABlankPatchNameIsReadAsUntitled) {
    feed(1000);
    bar_.update(13.0f, 0, "   ");
    EXPECT_TRUE(bar_.getAccessibilityText().startsWith("Untitled, "));
}

TEST_F(StatusBarAccessibilityTest, AMessageCoveringTheBarIsTheOnlyThingRead) {
    feed(1000);
    bar_.showStickyMessage("MIDI Learn: move a control");
    EXPECT_EQ(bar_.getAccessibilityText(), "MIDI Learn: move a control");
    bar_.clearMessage();
    EXPECT_TRUE(bar_.getAccessibilityText().startsWith("Project clips, CPU 13%"));
}

TEST_F(StatusBarAccessibilityTest, TheBarAndItsButtonsAreNamed) {
    EXPECT_FALSE(bar_.getTitle().isEmpty());
    EXPECT_FALSE(bar_.getTransportButton().getTitle().isEmpty());
}
