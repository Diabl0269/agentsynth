// CardStepperTests.cpp
//
// Source/UI/Graph/CardWidgets/CardStepper: the number field. One Tab stop named after the parameter; click an
// arrow to step, hold to repeat (the timer stops on mouseUp and focus loss), drag the number (Shift fine), type a
// value, Cmd-click to reset, keys step. On a card it drives its integer parameter, each gesture one undo step,
// stops at the range ends, and a right click opens the control's menu.

#include "../../../TestWait.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardWidgets/CardControlGestures.h"
#include "UI/Graph/CardWidgets/CardStepper.h"
#include "UI/Graph/CardWidgets/CardStepperBinding.h"

using namespace cardbody_test;
using synth::ui::CardControlGestures;
using synth::ui::CardStepper;

namespace {

constexpr int kStepCmd = juce::ModifierKeys::commandModifier;
constexpr int kStepShift = juce::ModifierKeys::shiftModifier;

juce::MouseEvent pressAt(CardStepper& s, juce::Point<int> p, int extra = 0, int clicks = 1) {
    return mouseAt(s, p.toFloat(), juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extra), clicks);
}

struct Field {
    CardStepper stepper{"Steps"};
    std::vector<int> steps;
    int begins = 0, ends = 0;
    Field() {
        stepper.setSize(88, 24);
        stepper.setVisible(true);
        stepper.setRange(1, 128, 8);
        stepper.setValue(16);
        stepper.setValueText("16");
        stepper.onGestureBegin = [this] { ++begins; };
        stepper.onGestureEnd = [this] { ++ends; };
        stepper.onStep = [this](int d) {
            steps.push_back(d); // a ranged field already moved its own value
            stepper.setValueText(juce::String(stepper.getValue()));
        };
    }
    juce::Point<int> centre(CardStepper::Zone z) { return stepper.zoneBounds(z).getCentre(); }
    int total() const {
        int sum = 0;
        for (int d : steps)
            sum += d;
        return sum;
    }
};

} // namespace

TEST(CardStepper, IsOneNamedTabStopWithAValueAndAGestureTooltip) {
    Field f;
    EXPECT_TRUE(f.stepper.getWantsKeyboardFocus());
    EXPECT_EQ(f.stepper.getTitle(), "Steps");
    EXPECT_TRUE(f.stepper.getTooltip().startsWith("Steps: "));
    EXPECT_TRUE(f.stepper.getTooltip().contains("drag"));
    EXPECT_TRUE(f.stepper.getTooltip().contains("Shift"));
    EXPECT_TRUE(f.stepper.getTooltip().contains("Cmd-click"));
    auto handler = static_cast<juce::Component&>(f.stepper).createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::slider);
    ASSERT_NE(handler->getValueInterface(), nullptr);
    EXPECT_EQ(handler->getValueInterface()->getCurrentValueAsString(), "16");
    for (auto* child : f.stepper.getChildren())
        EXPECT_FALSE(child->isVisible() && child->getWantsKeyboardFocus()) << "no second Tab stop";
}

TEST(CardStepper, KeysStepAndOtherKeysAreLeftToTheCard) {
    Field f;
    EXPECT_TRUE(f.stepper.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_TRUE(f.stepper.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_TRUE(f.stepper.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_FALSE(f.stepper.keyPressed(juce::KeyPress(juce::KeyPress::tabKey)));
    EXPECT_FALSE(f.stepper.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(f.steps, (std::vector<int>{+1, +1, -1}));
    EXPECT_EQ(f.begins, 3) << "each key is its own gesture";
    EXPECT_EQ(f.ends, 3);
}

TEST(CardStepper, AClickOnAnArrowStepsOnceAsOneGestureAndARightClickNever) {
    Field f;
    const auto up = f.centre(CardStepper::Zone::up);
    f.stepper.mouseDown(pressAt(f.stepper, up));
    EXPECT_EQ(f.steps, (std::vector<int>{+1}));
    f.stepper.mouseUp(pressAt(f.stepper, up));
    EXPECT_FALSE(f.stepper.isRepeating());
    f.stepper.mouseDown(pressAt(f.stepper, f.centre(CardStepper::Zone::down)));
    f.stepper.mouseUp(pressAt(f.stepper, f.centre(CardStepper::Zone::down)));
    EXPECT_EQ(f.steps, (std::vector<int>{+1, -1}));
    EXPECT_EQ(f.begins, 2);
    EXPECT_EQ(f.ends, 2);

    const juce::ModifierKeys right(juce::ModifierKeys::rightButtonModifier);
    f.stepper.mouseDown(mouseAt(f.stepper, up.toFloat(), right));
    f.stepper.mouseUp(mouseAt(f.stepper, up.toFloat(), right));
    EXPECT_EQ(f.steps.size(), 2u);
}

TEST(CardStepper, HoldingAnArrowRepeatsAfterThePauseAndStopsOnMouseUp) {
    Field f;
    const auto up = f.centre(CardStepper::Zone::up);
    f.stepper.mouseDown(pressAt(f.stepper, up));
    EXPECT_TRUE(f.stepper.isRepeating());
    EXPECT_EQ(f.steps.size(), 1u);
    // The pause is the first timer interval; a wall-clock bound on when the repeat lands would test the timer
    // thread's precision, which a loaded machine does not keep (it has fired 44 ms early on a busy CI runner).
    EXPECT_EQ(f.stepper.holdTimerIntervalMs(), CardControlGestures::kHoldPauseMs)
        << "the first repeat waits out the pause";
    EXPECT_FALSE(synth::test::waitUntil([&] { return f.steps.size() > 1; }, CardControlGestures::kHoldPauseMs / 4))
        << "no repeat straight away";
    synth::test::waitUntil([&] { return f.steps.size() > 1; });
    EXPECT_GT(f.steps.size(), 1u) << "repeating";
    f.stepper.mouseUp(pressAt(f.stepper, up));
    const auto taken = f.steps.size();
    EXPECT_FALSE(f.stepper.isRepeating());
    EXPECT_FALSE(synth::test::waitUntil([&] { return f.steps.size() > taken; }, 300)) << "no step after the release";
    EXPECT_EQ(f.begins, 1) << "the whole hold is one gesture";
    EXPECT_EQ(f.ends, 1);
}

TEST(CardStepper, LosingFocusStopsAHold) {
    Field f;
    f.stepper.mouseDown(pressAt(f.stepper, f.centre(CardStepper::Zone::up)));
    ASSERT_TRUE(f.stepper.isRepeating());
    f.stepper.focusLost(juce::Component::focusChangedDirectly);
    EXPECT_FALSE(f.stepper.isRepeating());
    EXPECT_EQ(f.ends, 1);
    const auto taken = f.steps.size();
    EXPECT_FALSE(synth::test::waitUntil([&] { return f.steps.size() > taken; }, 600));
}

TEST(CardStepper, TheRepeatSpeedsUpAfterThePause) {
    EXPECT_EQ(CardControlGestures::holdRepeatIntervalMs(400), 167);
    EXPECT_EQ(CardControlGestures::holdRepeatIntervalMs(2400), 33);
    EXPECT_LT(CardControlGestures::holdRepeatIntervalMs(1400), CardControlGestures::holdRepeatIntervalMs(400));
    EXPECT_EQ(CardControlGestures::holdRepeatIntervalMs(9000), 33) << "the top speed holds";
}

TEST(CardStepper, DraggingTheNumberUpStepsUpAboutThreePixelsAStepAsOneGesture) {
    Field f;
    f.stepper.setRange(0, 100, 0);
    f.stepper.setValue(10);
    const auto start = f.centre(CardStepper::Zone::number);
    f.stepper.mouseDown(pressAt(f.stepper, start));
    EXPECT_TRUE(f.steps.empty()) << "pressing the number without moving does nothing";
    f.stepper.mouseDrag(pressAt(f.stepper, start - juce::Point<int>(0, 30)));
    EXPECT_EQ(f.total(), 10) << "30 px up at 3 px a step";
    f.stepper.mouseDrag(pressAt(f.stepper, start + juce::Point<int>(0, 9)));
    EXPECT_EQ(f.total(), 10 - 13) << "down lowers it";
    f.stepper.mouseUp(pressAt(f.stepper, start));
    EXPECT_EQ(f.begins, 1);
    EXPECT_EQ(f.ends, 1);
    EXPECT_EQ(f.stepper.getValue(), 7);
}

TEST(CardStepper, ManySmallMovesAddUpToTheSameWholeStepsAsOneBigMove) {
    // Regression test for FRO1042: moves of 3 and 2 px at 3 px a step summed to 9.9999999999999 steps and lost the
    // tenth.
    Field f;
    f.stepper.setRange(0, 100, 0);
    f.stepper.setValue(10);
    const auto start = f.centre(CardStepper::Zone::number);
    f.stepper.mouseDown(pressAt(f.stepper, start));
    float travelled = 0.0f;
    for (int i = 0; i < 12; ++i) { // 6 x (3 + 2) px: exactly 10 steps
        travelled += (i % 2 == 0) ? 3.0f : 2.0f;
        f.stepper.mouseDrag(mouseAt(f.stepper, start.toFloat() - juce::Point<float>(0.0f, travelled),
                                    juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
    }
    f.stepper.mouseUp(pressAt(f.stepper, start));
    EXPECT_EQ(f.total(), 10);
}

TEST(CardStepper, ShiftDragIsATenthOfTheSpeedAndNeverJumpsWhenShiftChanges) {
    Field f;
    f.stepper.setRange(0, 100, 0);
    f.stepper.setValue(50);
    const auto start = f.centre(CardStepper::Zone::number);
    f.stepper.mouseDown(pressAt(f.stepper, start));
    f.stepper.mouseDrag(pressAt(f.stepper, start - juce::Point<int>(0, 30), kStepShift));
    EXPECT_EQ(f.total(), 1) << "30 px with Shift is one step";
    f.stepper.mouseDrag(pressAt(f.stepper, start - juce::Point<int>(0, 30)));
    EXPECT_EQ(f.total(), 1) << "letting go of Shift moves nothing by itself";
    f.stepper.mouseDrag(pressAt(f.stepper, start - juce::Point<int>(0, 33)));
    EXPECT_EQ(f.total(), 2) << "then full speed again";
    f.stepper.mouseUp(pressAt(f.stepper, start));
    EXPECT_FALSE(f.stepper.isRepeating());
}

TEST(CardStepper, DragPixelsPerStepFollowTheRangeWithinTheClamp) {
    EXPECT_DOUBLE_EQ(CardControlGestures::stepperDragPixelsPerStep(100), 3.0);
    EXPECT_NEAR(CardControlGestures::stepperDragPixelsPerStep(127), 2.362, 0.001);
    EXPECT_DOUBLE_EQ(CardControlGestures::stepperDragPixelsPerStep(1000), 2.0);
    EXPECT_DOUBLE_EQ(CardControlGestures::stepperDragPixelsPerStep(4), 8.0);
    EXPECT_DOUBLE_EQ(CardControlGestures::stepperDragPixelsPerStep(0), 3.0);
}

TEST(CardStepper, CommandClickOnTheNumberResetsToTheDefaultAsOneGesture) {
    Field f;
    f.stepper.mouseDown(pressAt(f.stepper, f.centre(CardStepper::Zone::number), kStepCmd));
    f.stepper.mouseUp(pressAt(f.stepper, f.centre(CardStepper::Zone::number), kStepCmd));
    EXPECT_EQ(f.steps, (std::vector<int>{8 - 16}));
    EXPECT_EQ(f.begins, 1);
    EXPECT_EQ(f.ends, 1);
    f.steps.clear();
    f.stepper.mouseDown(pressAt(f.stepper, f.centre(CardStepper::Zone::number), kStepCmd));
    f.stepper.mouseUp(pressAt(f.stepper, f.centre(CardStepper::Zone::number), kStepCmd));
    EXPECT_TRUE(f.steps.empty()) << "already at the default";
}

TEST(CardStepper, StepsStopAtTheRangeEnds) {
    Field f;
    f.stepper.setValue(128);
    f.stepper.mouseDown(pressAt(f.stepper, f.centre(CardStepper::Zone::up)));
    f.stepper.mouseUp(pressAt(f.stepper, f.centre(CardStepper::Zone::up)));
    EXPECT_TRUE(f.steps.empty()) << "nothing past the top";
    EXPECT_EQ(f.begins, 0) << "a blocked step opens no gesture";
    f.stepper.setValue(2);
    f.stepper.mouseDown(pressAt(f.stepper, f.centre(CardStepper::Zone::number)));
    f.stepper.mouseDrag(pressAt(f.stepper, f.centre(CardStepper::Zone::number) + juce::Point<int>(0, 60)));
    EXPECT_EQ(f.stepper.getValue(), 1) << "a long drag down lands on the bottom";
    f.stepper.mouseUp(pressAt(f.stepper, f.centre(CardStepper::Zone::number)));
}

TEST(CardStepper, DoubleClickingTheNumberOpensAnEditorThatCommitsOnReturnAndCancelsOnEscape) {
    Field f;
    juce::String typed;
    f.stepper.setTypable(true);
    f.stepper.onTypedValue = [&typed](const juce::String& t) { typed = t; };
    f.stepper.mouseDoubleClick(pressAt(f.stepper, f.centre(CardStepper::Zone::up), 0, 2));
    EXPECT_FALSE(f.stepper.isEditing()) << "an arrow does not type";
    f.stepper.mouseDoubleClick(pressAt(f.stepper, f.centre(CardStepper::Zone::number), 0, 2));
    ASSERT_TRUE(f.stepper.isEditing());
    EXPECT_TRUE(f.stepper.getTextEditor().isVisible());
    EXPECT_TRUE(f.stepper.getTextEditor().getBounds().intersects(f.stepper.zoneBounds(CardStepper::Zone::number)));
    EXPECT_EQ(f.stepper.getTextEditor().getText(), "16");
    f.stepper.getTextEditor().setText("24", false);
    f.stepper.getTextEditor().onReturnKey();
    EXPECT_EQ(typed, "24");
    EXPECT_FALSE(f.stepper.isEditing());

    typed.clear();
    f.stepper.showEditor();
    f.stepper.getTextEditor().setText("99", false);
    f.stepper.getTextEditor().onEscapeKey();
    EXPECT_FALSE(f.stepper.isEditing());
    EXPECT_TRUE(typed.isEmpty()) << "Escape cancels";
    synth::test::flushMessageQueue();
    EXPECT_FALSE(f.stepper.getTextEditor().isVisible());
}

TEST(CardStepper, ADigitKeyStartsTypingWhenTypable) {
    Field f;
    EXPECT_FALSE(f.stepper.keyPressed(juce::KeyPress('5'))) << "not typable: the key is not used";
    f.stepper.setTypable(true);
    EXPECT_TRUE(f.stepper.keyPressed(juce::KeyPress('5')));
    EXPECT_TRUE(f.stepper.isEditing());
    EXPECT_EQ(f.stepper.getTextEditor().getText(), "5");
}

TEST(CardStepper, TheVerticalVariantPutsTheArrowsAboveAndBelowAndTheNumberBetween) {
    CardStepper s("Steps");
    s.setOrientation(CardStepper::Orientation::vertical);
    s.setSize(44, 50);
    EXPECT_LT(s.zoneBounds(CardStepper::Zone::up).getY(), s.zoneBounds(CardStepper::Zone::number).getY());
    EXPECT_GT(s.zoneBounds(CardStepper::Zone::down).getY(), s.zoneBounds(CardStepper::Zone::number).getY());
    EXPECT_EQ(s.zoneAt(s.zoneBounds(CardStepper::Zone::up).getCentre()), CardStepper::Zone::up);
    EXPECT_TRUE(s.getTooltip().contains("above or below"));
}

TEST(CardStepper, TheIdealWidthIsTheWidestValuePlusTheArrows) {
    CardStepper s("Steps");
    s.setSize(88, 24);
    s.setWidestValueText("8");
    const int narrow = s.getIdealWidth();
    s.setWidestValueText("128");
    const int wide = s.getIdealWidth();
    EXPECT_GT(wide, narrow);
    EXPECT_GE(wide, 2 * 24 + 16);
    EXPECT_LT(s.getIdealWidth(18), wide) << "the compact field has smaller arrows";
}

TEST(CardStepper, OnACardItStepsItsIntegerAsOneUndoStepWithinItsRange) {
    CardCanvas canvas;
    auto osc = std::make_unique<OscillatorModule>();
    const auto layout = automaticLayoutWith(*osc, {{"octave", synth::CardWidget::Stepper}});
    const auto id = canvas.add(std::move(osc), 0, 0, layout);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* stepper = dynamic_cast<CardStepper*>(card->getCardBody()->findWidget("octave"));
    ASSERT_NE(stepper, nullptr);
    auto* octave = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(canvas.processor(id), "octave"));
    ASSERT_NE(octave, nullptr);
    EXPECT_EQ(card->findMidiLearnableParamForTest(stepper), octave);
    EXPECT_EQ(stepper->getValueText(), octave->getCurrentValueAsText());

    const int start = octave->get();
    const int height = card->getHeight();
    ASSERT_TRUE(stepper->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_EQ(octave->get(), start + 1);
    EXPECT_EQ(stepper->getValueText(), octave->getCurrentValueAsText());
    EXPECT_EQ(card->getHeight(), height);

    ASSERT_TRUE(canvas.undo.undo());
    auto* restored = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(canvas.processor(id), "octave"));
    EXPECT_EQ(restored->get(), start) << "one undo step";

    auto* current = dynamic_cast<CardStepper*>(canvas.card(id)->getCardBody()->findWidget("octave"));
    for (int i = 0; i < 20; ++i)
        current->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    EXPECT_EQ(restored->get(), restored->getRange().getEnd()) << "stops at the top of its range";

    // A right click on the field is the control's menu, not a step.
    const int top = restored->get();
    const auto menu = rightClickOnCard(*canvas.card(id), *current);
    EXPECT_NE(menuItem(menu, "Hide from card"), nullptr);
    EXPECT_EQ(restored->get(), top);
}

TEST(CardStepper, OnACardADragAndATypedValueAreEachOneUndoStep) {
    CardCanvas canvas;
    auto osc = std::make_unique<OscillatorModule>();
    const auto layout = automaticLayoutWith(*osc, {{"octave", synth::CardWidget::Stepper}});
    const auto id = canvas.add(std::move(osc), 0, 0, layout);
    canvas.editor.updateComponents();
    auto* stepper = dynamic_cast<CardStepper*>(canvas.card(id)->getCardBody()->findWidget("octave"));
    ASSERT_NE(stepper, nullptr);
    auto* octave = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(canvas.processor(id), "octave"));
    ASSERT_NE(octave, nullptr);
    stepper->setSize(88, 24);
    const int start = octave->get();
    const auto here = stepper->zoneBounds(CardStepper::Zone::number).getCentre();
    stepper->mouseDown(pressAt(*stepper, here));
    stepper->mouseDrag(pressAt(*stepper, here - juce::Point<int>(0, 8)));
    stepper->mouseDrag(pressAt(*stepper, here - juce::Point<int>(0, 16)));
    stepper->mouseUp(pressAt(*stepper, here));
    EXPECT_GT(octave->get(), start);
    ASSERT_TRUE(canvas.undo.undo());
    EXPECT_EQ(octave->get(), start) << "the whole drag was one undo step";

    auto* again = dynamic_cast<CardStepper*>(canvas.card(id)->getCardBody()->findWidget("octave"));
    EXPECT_TRUE(again->isTypable());
    again->onTypedValue("1");
    EXPECT_EQ(octave->get(), 1);
    EXPECT_EQ(again->getValueText(), octave->getCurrentValueAsText());
    ASSERT_TRUE(canvas.undo.undo());
    EXPECT_EQ(octave->get(), start);
}
