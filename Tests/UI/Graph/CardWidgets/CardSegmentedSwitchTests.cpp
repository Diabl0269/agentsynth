// CardSegmentedSwitchTests.cpp
//
// Source/UI/Graph/CardWidgets/CardSegmentedSwitch: a choice as joined segments. One Tab stop that
// Left/Right/Home/End move; a click picks the segment under it; a right click picks nothing; a screen
// reader sees a group of titled radio buttons with the selected one checked. On a card it drives its
// choice parameter as one undo step and follows the parameter back.

#include "../../../TestWait.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

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
    synth::test::waitUntil([&] { return (s.control.getSelectedIndex()) == (3); });
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

// Declared here, not included: BuiltInThemes.h is at the header-reach cap and one more includer pushes it over.
namespace synth::theme {
std::vector<Theme> builtInThemes();
}

namespace {

bool isAccentPixel(juce::Colour pixel, juce::Colour accent) {
    return pixel.getAlpha() > 200 && std::abs(pixel.getFloatRed() - accent.getFloatRed()) < 0.12f &&
           std::abs(pixel.getFloatGreen() - accent.getFloatGreen()) < 0.12f &&
           std::abs(pixel.getFloatBlue() - accent.getFloatBlue()) < 0.12f;
}

// Pixels between the ring and the lit fill along a ray from the switch's top-left corner (step (1,1) for the diagonal,
// (1,0) at mid height), as a distance in px.
float bandAlong(const juce::Image& image, juce::Colour accent, juce::Point<int> from, juce::Point<int> step) {
    int phase = 0; // 0: before the ring, 1: in the ring, 2: in the band, 3: in the fill
    int band = 0;
    for (auto p = from; image.getBounds().contains(p) && phase < 3; p += step) {
        const bool accentHere = isAccentPixel(image.getPixelAt(p.x, p.y), accent);
        if (phase == 0 && accentHere)
            phase = 1;
        else if (phase == 1 && !accentHere)
            phase = 2;
        if (phase == 2) {
            if (accentHere)
                phase = 3;
            else
                ++band;
        }
    }
    return (float)band * (step.x != 0 && step.y != 0 ? 1.41421356f : 1.0f);
}

} // namespace

// Regression test for FRO2220: the focus ring was a fixed radius 4 box while the lit tab took the look-and-feel radius,
// so the band between them was wider at the corner than along the edge.
TEST(CardSegmentedSwitch, TheFocusRingAndTheLitTabAreConcentricInEveryTheme) {
    synth::ui::setFocusRingsShownForTest(true);
    for (const auto& theme : synth::theme::builtInThemes()) {
        SCOPED_TRACE(theme.name.toStdString());
        synth::theme::AppLookAndFeel lookAndFeel;
        lookAndFeel.applyTheme(theme);
        LoneSwitch s;
        s.control.setLookAndFeel(&lookAndFeel);
        s.control.setSize(240, 32);
        s.control.setSelectedIndex(0, juce::dontSendNotification);
        juce::Image image(juce::Image::ARGB, 240, 32, true, juce::SoftwareImageType());
        {
            juce::Graphics g(image);
            s.control.paintEntireComponent(g, false);
        }
        const float edge = bandAlong(image, theme.colors.accent, {0, 16}, {1, 0});
        const float corner = bandAlong(image, theme.colors.accent, {0, 0}, {1, 1});
        EXPECT_GT(edge, 0.0f) << "a band of surface shows between the ring and the lit tab";
        EXPECT_NEAR(corner, edge, 2.0f) << "the band is as wide at the corner as along the edge";
        s.control.setLookAndFeel(nullptr);
    }
    synth::ui::setFocusRingsShownForTest(false);
}
