// The drum machine's groove controls: the Swing and Humanise knobs (0 to 100%) and the swung hits' flash timing. The
// lanes under the grid are in DrumMachineLanesTests.cpp.

#include "DrumMachineGrooveFixture.h"

using drumtest::AnimationModeGuard;
using drumtest::GrooveFixture;

//==============================================================================
// Swing

TEST(DrumMachineSwingTest, TheKnobIsNamedAndTipped) {
    GrooveFixture f;
    auto& knob = f.grid.getSwingKnob();
    EXPECT_EQ(knob.getTitle(), "Swing");
    EXPECT_EQ(knob.getTooltip(), "Swing: push every second step later (0% is straight)");
    EXPECT_TRUE(knob.getWantsKeyboardFocus());
    knob.setValue(56.0, juce::dontSendNotification);
    EXPECT_EQ(knob.getTextFromValue(knob.getValue()), "56%");
    EXPECT_DOUBLE_EQ(knob.getMinimum(), 0.0);
    EXPECT_DOUBLE_EQ(knob.getMaximum(), 100.0);
}

TEST(DrumMachineSwingTest, TheKnobMapsZeroToStraightAndHundredToTheFullShuffle) {
    GrooveFixture f;
    auto& knob = f.grid.getSwingKnob();
    EXPECT_DOUBLE_EQ(knob.getValue(), 0.0) << "a straight clip reads 0%";
    EXPECT_TRUE(knob.keyPressed(juce::KeyPress(juce::KeyPress::endKey)));
    EXPECT_DOUBLE_EQ(knob.getValue(), 100.0);
    EXPECT_DOUBLE_EQ(f.c().swing, 0.75);
    EXPECT_TRUE(knob.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)));
    EXPECT_DOUBLE_EQ(knob.getValue(), 0.0);
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5);
    knob.setValue(50.0, juce::sendNotificationSync);
    EXPECT_DOUBLE_EQ(f.c().swing, 0.625);
    EXPECT_DOUBLE_EQ(DrumMachineComponent::swingToPercent(0.5), 0.0);
    EXPECT_DOUBLE_EQ(DrumMachineComponent::percentToSwing(100.0), 0.75);
}

TEST(DrumMachineSwingTest, AClipAtSixtySixHundredthsReadsSixtyFourPercent) {
    GrooveFixture f;
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.66));
    f.grid.refreshFromDoc();
    auto& knob = f.grid.getSwingKnob();
    EXPECT_EQ(knob.getTextFromValue(knob.getValue()), "64%");
    EXPECT_DOUBLE_EQ(f.c().swing, 0.66) << "showing it does not rewrite it";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineSwingTest, DoubleClickingTheKnobResetsToStraightAsOneUndoStep) {
    GrooveFixture f;
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.7));
    f.grid.refreshFromDoc();
    auto& knob = f.grid.getSwingKnob();
    knob.mouseDoubleClick(leftClick(knob, centreOf(knob.getLocalBounds())));
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5);
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.c().swing, 0.7);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineSwingTest, ADragOnTheKnobIsOneUndoStepCommittedOnRelease) {
    GrooveFixture f;
    auto& knob = f.grid.getSwingKnob();
    ASSERT_FALSE(knob.getBounds().isEmpty());
    const auto start = centreOf(knob.getLocalBounds());
    knob.mouseDown(leftClick(knob, start));
    for (int dy = -20; dy >= -100; dy -= 20)
        knob.mouseDrag(leftDrag(knob, start + juce::Point<float>(0.0f, (float)dy), start));
    EXPECT_GT(knob.getValue(), 0.0);
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5) << "nothing is written while the knob is held";
    knob.mouseUp(leftDrag(knob, start + juce::Point<float>(0.0f, -100.0f), start));
    EXPECT_GT(f.c().swing, 0.5);
    EXPECT_NEAR(DrumMachineComponent::swingToPercent(f.c().swing), knob.getValue(), 1e-9);
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole turn was one undo step";
    f.grid.refreshFromDoc(); // the panel does this when the timeline changes
    EXPECT_DOUBLE_EQ(f.grid.getSwingKnob().getValue(), 0.0) << "the knob follows the undo";
}

TEST(DrumMachineSwingTest, AKeyPressOnTheKnobIsOneUndoStep) {
    GrooveFixture f;
    auto& knob = f.grid.getSwingKnob();
    EXPECT_TRUE(knob.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5025) << "one percent of the knob is 0.0025 of swing";
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5);
    EXPECT_FALSE(f.undo.canUndo());
}

//==============================================================================
// Humanise

TEST(DrumMachineHumaniseTest, TheKnobIsNamedTippedAndReadsZeroToHundredPercent) {
    GrooveFixture f;
    auto& knob = f.grid.getHumaniseKnob();
    EXPECT_EQ(knob.getTitle(), "Humanise");
    EXPECT_EQ(knob.getTooltip(), "Humanise: small random changes to each hit's loudness and timing (0% is exact)");
    EXPECT_TRUE(knob.getWantsKeyboardFocus());
    EXPECT_TRUE(knob.isVisible());
    EXPECT_FALSE(knob.getBounds().isEmpty());
    EXPECT_DOUBLE_EQ(knob.getMinimum(), 0.0);
    EXPECT_DOUBLE_EQ(knob.getMaximum(), 100.0);
    knob.setValue(35.0, juce::dontSendNotification);
    EXPECT_EQ(knob.getTextFromValue(knob.getValue()), "35%");
}

TEST(DrumMachineHumaniseTest, ADragIsOneUndoStepCommittedOnRelease) {
    GrooveFixture f;
    auto& knob = f.grid.getHumaniseKnob();
    const auto start = centreOf(knob.getLocalBounds());
    knob.mouseDown(leftClick(knob, start));
    for (int dy = -20; dy >= -100; dy -= 20)
        knob.mouseDrag(leftDrag(knob, start + juce::Point<float>(0.0f, (float)dy), start));
    EXPECT_GT(knob.getValue(), 0.0);
    EXPECT_DOUBLE_EQ(f.c().humanise, 0.0) << "nothing is written while the knob is held";
    knob.mouseUp(leftDrag(knob, start + juce::Point<float>(0.0f, -100.0f), start));
    EXPECT_GT(f.c().humanise, 0.0);
    EXPECT_NEAR(f.c().humanise * 100.0, knob.getValue(), 1e-9);
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.c().humanise, 0.0);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole turn was one undo step";
    f.grid.refreshFromDoc();
    EXPECT_DOUBLE_EQ(f.grid.getHumaniseKnob().getValue(), 0.0) << "the knob follows the undo";
}

TEST(DrumMachineHumaniseTest, AKeyPressIsOneUndoStep) {
    GrooveFixture f;
    auto& knob = f.grid.getHumaniseKnob();
    EXPECT_TRUE(knob.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_DOUBLE_EQ(f.c().humanise, 0.01);
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.c().humanise, 0.0);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineHumaniseTest, DoubleClickResetsToZeroAsOneUndoStepAndDoesNothingWhenAlreadyZero) {
    GrooveFixture f;
    auto& knob = f.grid.getHumaniseKnob();
    knob.mouseDoubleClick(leftClick(knob, centreOf(knob.getLocalBounds())));
    EXPECT_FALSE(f.undo.canUndo()) << "already 0%: no empty undo step";

    ASSERT_TRUE(f.doc.setClipHumanise(f.clip, 0.4));
    f.grid.refreshFromDoc();
    EXPECT_DOUBLE_EQ(knob.getValue(), 40.0);
    knob.mouseDoubleClick(leftClick(knob, centreOf(knob.getLocalBounds())));
    EXPECT_DOUBLE_EQ(f.c().humanise, 0.0);
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.c().humanise, 0.4);
    EXPECT_FALSE(f.undo.canUndo()) << "the reset was one undo step";
}

TEST(DrumMachineSwingTest, ASwungHitShowsATickAndAStraightOneDoesNot) {
    GrooveFixture f;
    f.addHits(36, {1});
    const auto cell = f.grid.cellBounds(f.grid.rowOfPitch(36), 1);
    const auto pixelAtTick = [&] {
        juce::Image image(juce::Image::ARGB, f.grid.getWidth(), f.grid.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        f.grid.paintEntireComponent(g, false);
        return image.getPixelAt(cell.getX() + cell.getWidth() / 2, cell.getCentreY());
    };
    const auto straight = pixelAtTick();
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.75));
    f.grid.refreshFromDoc();
    const auto swung = pixelAtTick();
    EXPECT_GT(swung.getGreen(), straight.getGreen()) << "the tick at half a step is lighter than the hit";
}

TEST(DrumMachineSwingTest, ASwungHitFlashesWhenItPlaysNotWhenTheColumnIsEntered) {
    AnimationModeGuard fullMotion(synth::ui::AnimationMode::full);
    GrooveFixture f;
    f.addHits(42, {1});
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.75));
    f.grid.refreshFromDoc();
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    const int row = f.grid.rowOfPitch(42);
    f.grid.setTransportPlaying(true);
    f.grid.setPlayheadBeat(0.0);
    f.grid.setPlayheadBeat(0.30); // step 1 begins at 0.25 and its hit plays at 0.375
    EXPECT_FALSE(f.grid.isFlashActive(row, 1));
    f.grid.setPlayheadBeat(0.40);
    EXPECT_TRUE(f.grid.isFlashActive(row, 1));
    f.grid.finishMotionsForTest();
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
}
