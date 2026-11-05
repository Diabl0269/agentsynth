// The drum machine's groove controls: the Swing field and the Humanise knob (0 to 100%) and the swung hits' flash
// timing. The lanes under the grid are in DrumMachineLanesTests.cpp.

#include "DrumMachineGrooveFixture.h"

using drumtest::AnimationModeGuard;
using drumtest::GrooveFixture;

//==============================================================================
// Swing

using synth::ui::CardStepper;

namespace {
juce::Point<int> numberOf(CardStepper& field) { return field.zoneBounds(CardStepper::Zone::number).getCentre(); }
juce::MouseEvent swingPress(CardStepper& field, juce::Point<int> at, int extra = 0, bool dragged = false) {
    return dragged ? leftDrag(field, at.toFloat(), numberOf(field).toFloat(), extra)
                   : leftClick(field, at.toFloat(), extra);
}
} // namespace

TEST(DrumMachineSwingTest, TheFieldIsNamedTippedAndReadsZeroToHundredPercent) {
    GrooveFixture f;
    auto& field = f.grid.getSwingField();
    EXPECT_EQ(field.getTitle(), "Swing");
    EXPECT_TRUE(field.getTooltip().startsWith("Swing: push every second step later (0% is straight)"));
    EXPECT_TRUE(field.getTooltip().contains("drag the number"));
    EXPECT_TRUE(field.getWantsKeyboardFocus());
    EXPECT_TRUE(field.isTypable());
    EXPECT_EQ(field.getValueText(), "0%");
    EXPECT_FALSE(field.getBounds().isEmpty());
    EXPECT_EQ(field.getHeight(), 18);
    EXPECT_EQ(field.getWidth(), field.getIdealWidth(18)) << "as wide as 100% needs, plus the arrows";
}

TEST(DrumMachineSwingTest, TheFieldMapsZeroToStraightAndHundredToTheFullShuffle) {
    GrooveFixture f;
    auto& field = f.grid.getSwingField();
    field.onTypedValue("100");
    EXPECT_EQ(field.getValue(), 100);
    EXPECT_DOUBLE_EQ(f.c().swing, 0.75);
    EXPECT_EQ(field.getValueText(), "100%");
    field.onTypedValue("0%");
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5);
    field.onTypedValue("50");
    EXPECT_DOUBLE_EQ(f.c().swing, 0.625);
    field.onTypedValue("250");
    EXPECT_DOUBLE_EQ(f.c().swing, 0.75) << "a typed value past the end lands on 100%";
    field.onTypedValue("soon");
    EXPECT_DOUBLE_EQ(f.c().swing, 0.75) << "text with no number changes nothing";
    EXPECT_DOUBLE_EQ(DrumMachineComponent::swingToPercent(0.5), 0.0);
    EXPECT_DOUBLE_EQ(DrumMachineComponent::percentToSwing(100.0), 0.75);
}

TEST(DrumMachineSwingTest, AClipAtSixtySixHundredthsReadsSixtyFourPercent) {
    GrooveFixture f;
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.66));
    f.grid.refreshFromDoc();
    EXPECT_EQ(f.grid.getSwingField().getValueText(), "64%");
    EXPECT_DOUBLE_EQ(f.c().swing, 0.66) << "showing it does not rewrite it";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineSwingTest, CommandClickingTheNumberResetsToStraightAsOneUndoStep) {
    GrooveFixture f;
    ASSERT_TRUE(f.doc.setClipSwing(f.clip, 0.7));
    f.grid.refreshFromDoc();
    auto& field = f.grid.getSwingField();
    field.mouseDown(swingPress(field, numberOf(field), juce::ModifierKeys::commandModifier));
    field.mouseUp(swingPress(field, numberOf(field), juce::ModifierKeys::commandModifier));
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5);
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.c().swing, 0.7);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineSwingTest, ADragOnTheNumberIsOneUndoStepCommittedOnRelease) {
    GrooveFixture f;
    auto& field = f.grid.getSwingField();
    const auto start = numberOf(field);
    field.mouseDown(swingPress(field, start));
    for (int dy = -6; dy >= -30; dy -= 6)
        field.mouseDrag(swingPress(field, start + juce::Point<int>(0, dy), 0, true));
    EXPECT_EQ(field.getValue(), 10) << "30 px up at 3 px a percent";
    EXPECT_EQ(field.getValueText(), "10%");
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5) << "nothing is written while the number is held";
    field.mouseUp(swingPress(field, start + juce::Point<int>(0, -30)));
    EXPECT_DOUBLE_EQ(f.c().swing, DrumMachineComponent::percentToSwing(10.0));
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole drag was one undo step";
    f.grid.refreshFromDoc(); // the panel does this when the timeline changes
    EXPECT_EQ(f.grid.getSwingField().getValue(), 0) << "the field follows the undo";
}

TEST(DrumMachineSwingTest, AShiftDragMovesOneEighthAsFast) {
    GrooveFixture f;
    auto& field = f.grid.getSwingField();
    const auto start = numberOf(field);
    field.mouseDown(swingPress(field, start));
    field.mouseDrag(swingPress(field, start + juce::Point<int>(0, -24), juce::ModifierKeys::shiftModifier, true));
    field.mouseUp(swingPress(field, start));
    EXPECT_EQ(f.grid.getSwingField().getValue(), 1);
    EXPECT_DOUBLE_EQ(f.c().swing, DrumMachineComponent::percentToSwing(1.0));
}

TEST(DrumMachineSwingTest, ClickingAnArrowIsOnePercentAndOneUndoStep) {
    GrooveFixture f;
    auto& field = f.grid.getSwingField();
    const auto up = field.zoneBounds(CardStepper::Zone::up).getCentre();
    field.mouseDown(swingPress(field, up));
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5) << "written when the press ends";
    field.mouseUp(swingPress(field, up));
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5025) << "one percent of the field is 0.0025 of swing";
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5);
    EXPECT_FALSE(f.undo.canUndo());
    const auto down = field.zoneBounds(CardStepper::Zone::down).getCentre();
    field.mouseDown(swingPress(field, down));
    field.mouseUp(swingPress(field, down));
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5) << "nothing below 0%";
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineSwingTest, AKeyPressOnTheFieldIsOneUndoStep) {
    GrooveFixture f;
    auto& field = f.grid.getSwingField();
    EXPECT_TRUE(field.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5025);
    f.undo.undo();
    EXPECT_DOUBLE_EQ(f.c().swing, 0.5);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(DrumMachineSwingTest, TypingAValueIsOneUndoStep) {
    GrooveFixture f;
    auto& field = f.grid.getSwingField();
    field.mouseDoubleClick(swingPress(field, numberOf(field)));
    ASSERT_TRUE(field.isEditing());
    field.getTextEditor().setText("54", false);
    field.getTextEditor().onReturnKey();
    EXPECT_DOUBLE_EQ(f.c().swing, DrumMachineComponent::percentToSwing(54.0));
    EXPECT_EQ(field.getValueText(), "54%");
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
