// CardSegmentedSwitchTests.cpp
//
// Source/UI/Graph/CardWidgets/CardSegmentedSwitch: a choice as joined segments. One Tab stop that
// Left/Right/Home/End move; a click picks the segment under it; a right click picks nothing; a screen
// reader sees a group of titled radio buttons with the selected one checked. On a card it drives its
// choice parameter as one undo step and follows the parameter back.

#include "../CardBody/CardBodyTestHelpers.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"

using namespace cardbody_test;
using synth::ui::CardSegmentedSwitch;

namespace {

struct LoneSwitch {
    CardSegmentedSwitch control{"Waveform", {"Sine", "Square", "Saw", "Triangle"}};
    std::vector<int> picks;

    LoneSwitch() {
        control.setSize(240, 24);
        control.setSelectedIndex(1, juce::dontSendNotification);
        control.onChange = [this](int index) { picks.push_back(index); };
    }
};

} // namespace

TEST(CardSegmentedSwitch, ArrowsHomeAndEndMoveTheSelectionFromOneTabStop) {
    LoneSwitch s;
    EXPECT_TRUE(s.control.getWantsKeyboardFocus());
    for (int i = 0; i < s.control.getNumSegments(); ++i)
        EXPECT_FALSE(s.control.getSegment(i)->getWantsKeyboardFocus()) << "segments are not Tab stops";

    EXPECT_TRUE(s.control.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(s.control.getSelectedIndex(), 2);
    EXPECT_TRUE(s.control.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_TRUE(s.control.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_EQ(s.control.getSelectedIndex(), 0);
    EXPECT_TRUE(s.control.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_EQ(s.control.getSelectedIndex(), 0) << "clamped at the first segment";
    EXPECT_TRUE(s.control.keyPressed(juce::KeyPress(juce::KeyPress::endKey)));
    EXPECT_EQ(s.control.getSelectedIndex(), 3);
    EXPECT_FALSE(s.control.keyPressed(juce::KeyPress(juce::KeyPress::tabKey)));
    EXPECT_EQ(s.picks, (std::vector<int>{2, 1, 0, 3})) << "one notification per change, none at the clamp";
}

TEST(CardSegmentedSwitch, AClickPicksTheSegmentUnderItAndARightClickPicksNothing) {
    LoneSwitch s;
    const auto saw = s.control.getSegment(2)->getBounds().getCentre().toFloat();
    s.control.mouseDown(mouseAt(s.control, saw, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
    EXPECT_EQ(s.control.getSelectedIndex(), 1);
    s.control.mouseDown(mouseAt(s.control, saw, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
    EXPECT_EQ(s.control.getSelectedIndex(), 2);
    EXPECT_EQ(s.picks, (std::vector<int>{2}));
}

TEST(CardSegmentedSwitch, ScreenReaderSeesAGroupOfTitledRadioButtons) {
    LoneSwitch s;
    // Built directly: getAccessibilityHandler() needs a native window, which a headless test has not.
    auto handler = s.control.createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::group);
    EXPECT_EQ(handler->getTitle(), "Waveform");
    const juce::StringArray values{"Sine", "Square", "Saw", "Triangle"};
    for (int i = 0; i < s.control.getNumSegments(); ++i) {
        auto segment = static_cast<juce::Component*>(s.control.getSegment(i))->createAccessibilityHandler();
        ASSERT_NE(segment, nullptr);
        EXPECT_EQ(segment->getRole(), juce::AccessibilityRole::radioButton);
        EXPECT_EQ(segment->getTitle(), values[i]);
        EXPECT_EQ(segment->getCurrentState().isChecked(), i == 1);
        EXPECT_TRUE(s.control.getSegment(i)->getTooltip().contains(values[i]));
    }

    // An accessibility press on a segment selects it.
    auto press = static_cast<juce::Component*>(s.control.getSegment(3))->createAccessibilityHandler();
    ASSERT_TRUE(press->getActions().invoke(juce::AccessibilityActionType::press));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    EXPECT_EQ(s.control.getSelectedIndex(), 3);
}

TEST(CardSegmentedSwitch, OnACardItDrivesItsChoiceAsOneUndoStepAndFollowsItBack) {
    CardCanvas canvas;
    auto osc = std::make_unique<OscillatorModule>();
    const auto layout = automaticLayoutWith(*osc, {{"waveform", synth::CardWidget::Segmented}});
    const auto id = canvas.add(std::move(osc), 0, 0, layout);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* control = dynamic_cast<CardSegmentedSwitch*>(card->getCardBody()->findWidget("waveform"));
    ASSERT_NE(control, nullptr);
    auto* waveform = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(canvas.processor(id), "waveform"));
    ASSERT_NE(waveform, nullptr);
    EXPECT_EQ(control->getSelectedIndex(), waveform->getIndex());
    EXPECT_EQ(card->findMidiLearnableParamForTest(control), waveform);
    EXPECT_EQ(control->getHeight(), synth::cardbody::kRowHeight);

    const int start = waveform->getIndex();
    const int height = card->getHeight();
    ASSERT_TRUE(control->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(waveform->getIndex(), start + 1);
    EXPECT_EQ(card->getHeight(), height) << "a value change never resizes the card";

    ASSERT_TRUE(canvas.undo.undo());
    auto* restored = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(canvas.processor(id), "waveform"));
    EXPECT_EQ(restored->getIndex(), start) << "one undo step";

    restored->setValueNotifyingHost(restored->convertTo0to1(3.0f));
    auto* current = dynamic_cast<CardSegmentedSwitch*>(canvas.card(id)->getCardBody()->findWidget("waveform"));
    EXPECT_EQ(current->getSelectedIndex(), 3) << "the switch follows the parameter";

    const auto menu = rightClickOnCard(*canvas.card(id), *current);
    EXPECT_NE(menuItem(menu, "Hide from card"), nullptr);
    EXPECT_EQ(menuItem(menu, "Show as fader"), nullptr) << "a choice has no fader";
    EXPECT_EQ(current->getSelectedIndex(), 3) << "a right click picks nothing";
}
