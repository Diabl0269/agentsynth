// CardStepperTests.cpp
//
// Source/UI/Graph/CardWidgets/CardStepper: a small integer as "-" value "+". Each button is a titled Tab
// stop, the arrows step while either has focus, a click steps once, a right click never steps. On a
// card it drives its integer parameter as one undo step, stops at the range ends, and a right click on
// either button opens the control's menu.

#include "../CardBody/CardBodyTestHelpers.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardWidgets/CardStepper.h"

using namespace cardbody_test;
using synth::ui::CardStepper;

TEST(CardStepper, ButtonsAreTitledTabStopsAndArrowsStep) {
    CardStepper stepper("Octave");
    std::vector<int> steps;
    stepper.onStep = [&steps](int delta) { steps.push_back(delta); };
    stepper.setSize(200, 24);

    EXPECT_TRUE(stepper.getDownButton().getWantsKeyboardFocus());
    EXPECT_TRUE(stepper.getUpButton().getWantsKeyboardFocus());
    EXPECT_EQ(stepper.getDownButton().getTitle(), "Octave down");
    EXPECT_EQ(stepper.getUpButton().getTitle(), "Octave up");
    EXPECT_TRUE(stepper.getDownButton().getTooltip().startsWith("Octave down"));
    EXPECT_TRUE(stepper.getUpButton().getTooltip().startsWith("Octave up"));
    EXPECT_EQ(static_cast<juce::Component&>(stepper.getDownButton()).createAccessibilityHandler()->getRole(),
              juce::AccessibilityRole::button);

    EXPECT_TRUE(stepper.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_TRUE(stepper.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_TRUE(stepper.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_FALSE(stepper.keyPressed(juce::KeyPress(juce::KeyPress::tabKey)));
    EXPECT_EQ(steps, (std::vector<int>{+1, +1, -1}));

    stepper.setValueText("2");
    EXPECT_EQ(stepper.getValueText(), "2");
}

TEST(CardStepper, AClickStepsOnceAndARightClickNever) {
    CardStepper stepper("Octave");
    std::vector<int> steps;
    stepper.onStep = [&steps](int delta) { steps.push_back(delta); };
    stepper.setSize(200, 24);

    auto& up = stepper.getUpButton();
    auto& upComponent = static_cast<juce::Component&>(up);
    const juce::ModifierKeys right(juce::ModifierKeys::rightButtonModifier);
    upComponent.mouseDown(mouseAt(up, up.getLocalBounds().getCentre().toFloat(), right));
    upComponent.mouseUp(mouseAt(up, up.getLocalBounds().getCentre().toFloat(), right));
    EXPECT_TRUE(steps.empty());

    up.triggerClick();
    stepper.getDownButton().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_EQ(steps, (std::vector<int>{+1, -1}));
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

    // A right click on the "+" button is the control's menu, not a step.
    const int top = restored->get();
    const auto menu = rightClickOnCard(*canvas.card(id), current->getUpButton());
    EXPECT_NE(menuItem(menu, "Hide from card"), nullptr);
    EXPECT_EQ(restored->get(), top);
}
