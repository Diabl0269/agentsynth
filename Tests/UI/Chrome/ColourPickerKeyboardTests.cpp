// ColourPickerKeyboardTests.cpp -- the colour picker is reachable and operable from the keyboard: the saturation and
// brightness square and the hue bar take Tab, step on the arrow keys (one percent or degree, ten with Shift, Home and
// End to the ends), change the colour through the same live preview as a slider, and follow the sliders both ways. The
// selector's sliders take the arrows too. Every stop has a screen-reader name, a value and a tooltip.
#include "../../TestWait.h"
#include "../Accessibility/TabOrderHelpers.h"
#include "UI/Chrome/ColourPickerFields.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Layout/FocusRing.h"
#include <gtest/gtest.h>
#include <vector>

using namespace synth::ui;

namespace {

juce::KeyPress key(int code, bool shift = false) {
    return juce::KeyPress(code, shift ? juce::ModifierKeys(juce::ModifierKeys::shiftModifier) : juce::ModifierKeys(),
                          0);
}

void collectSliders(juce::Component& root, std::vector<juce::Slider*>& out) {
    for (auto* child : root.getChildren()) {
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            out.push_back(slider);
        else
            collectSliders(*child, out);
    }
}

juce::Slider* sliderNamed(juce::Component& root, const juce::String& name) {
    std::vector<juce::Slider*> sliders;
    collectSliders(root, sliders);
    for (auto* slider : sliders)
        if (slider->getName() == name)
            return slider;
    return nullptr;
}

// Runs the messages already posted, and those they post in turn (the selector re-names its sliders in a second hop).
void settle() {
    for (int pass = 0; pass < 3; ++pass)
        synth::test::flushMessageQueue();
}

struct Picker {
    explicit Picker(juce::Colour initial)
        : popup(initial, nullptr, [this](juce::Colour c) { previews.push_back(c); }, {}) {
        popup.setSize(280, 312);
        sb = dynamic_cast<SaturationBrightnessField*>(popup.findChildWithID("colourPickerSaturationBrightness"));
        hue = dynamic_cast<HueBar*>(popup.findChildWithID("colourPickerHue"));
    }
    std::vector<juce::Colour> previews;
    ColourPickerPopup popup;
    SaturationBrightnessField* sb = nullptr;
    HueBar* hue = nullptr;
};

} // namespace

TEST(ColourPickerKeyboard, TheSaturationBrightnessFieldStepsOnePercentAndTenWithShiftAndPreviewsLive) {
    Picker p(juce::Colours::red);
    ASSERT_NE(p.sb, nullptr);
    EXPECT_TRUE(p.sb->keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_EQ(p.sb->valueText(), "Saturation 99%, brightness 100%");
    EXPECT_EQ(p.popup.getCurrentColourForTest(), juce::Colour(0.0f, 0.99f, 1.0f, 1.0f));
    ASSERT_FALSE(p.previews.empty());
    EXPECT_EQ(p.previews.back(), p.popup.getCurrentColourForTest()) << "the arrow previews like a slider does";

    EXPECT_TRUE(p.sb->keyPressed(key(juce::KeyPress::leftKey, true)));
    EXPECT_EQ(p.sb->valueText(), "Saturation 89%, brightness 100%");
    EXPECT_TRUE(p.sb->keyPressed(key(juce::KeyPress::downKey)));
    EXPECT_EQ(p.sb->valueText(), "Saturation 89%, brightness 99%");
    EXPECT_TRUE(p.sb->keyPressed(key(juce::KeyPress::downKey, true)));
    EXPECT_EQ(p.sb->valueText(), "Saturation 89%, brightness 89%");
    EXPECT_TRUE(p.sb->keyPressed(key(juce::KeyPress::upKey)));
    EXPECT_TRUE(p.sb->keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_EQ(p.sb->valueText(), "Saturation 90%, brightness 90%");
    EXPECT_TRUE(p.sb->keyPressed(key(juce::KeyPress::homeKey)));
    EXPECT_EQ(p.sb->hsb().saturation, 0.0f);
    EXPECT_TRUE(p.sb->keyPressed(key(juce::KeyPress::endKey)));
    EXPECT_EQ(p.sb->hsb().saturation, 1.0f);
    EXPECT_FALSE(p.sb->keyPressed(key(juce::KeyPress::escapeKey))) << "Escape goes on to the popover";
    EXPECT_FALSE(p.sb->keyPressed(key(juce::KeyPress::tabKey))) << "Tab moves focus";
}

TEST(ColourPickerKeyboard, TheHueBarStepsOneDegreeAndTenWithShiftAndStopsAtTheEnds) {
    Picker p(juce::Colours::red);
    ASSERT_NE(p.hue, nullptr);
    EXPECT_EQ(p.hue->valueText(), "Hue 0 degrees");
    EXPECT_TRUE(p.hue->keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_EQ(p.hue->valueText(), "Hue 1 degrees");
    EXPECT_EQ(p.popup.getCurrentColourForTest(), juce::Colour(1.0f / 360.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(p.hue->keyPressed(key(juce::KeyPress::downKey, true)));
    EXPECT_EQ(p.hue->valueText(), "Hue 11 degrees");
    EXPECT_TRUE(p.hue->keyPressed(key(juce::KeyPress::upKey)));
    EXPECT_TRUE(p.hue->keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_EQ(p.hue->valueText(), "Hue 9 degrees");
    EXPECT_TRUE(p.hue->keyPressed(key(juce::KeyPress::endKey)));
    EXPECT_EQ(p.hue->valueText(), "Hue 360 degrees");
    EXPECT_TRUE(p.hue->keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_EQ(p.hue->valueText(), "Hue 360 degrees");
    EXPECT_TRUE(p.hue->keyPressed(key(juce::KeyPress::homeKey)));
    EXPECT_EQ(p.hue->valueText(), "Hue 0 degrees");
    EXPECT_FALSE(p.hue->keyPressed(key(juce::KeyPress::escapeKey)));
}

TEST(ColourPickerKeyboard, TheFieldsFollowTheSlidersAndTheSlidersFollowTheFields) {
    Picker p(juce::Colours::red);
    p.popup.setCurrentColourForTest(juce::Colours::blue);
    EXPECT_EQ(p.hue->valueText(), "Hue 240 degrees");
    EXPECT_EQ(p.sb->valueText(), "Saturation 100%, brightness 100%");

    auto* red = sliderNamed(p.popup, "red");
    ASSERT_NE(red, nullptr);
    red->setValue(255.0, juce::sendNotificationSync);
    settle();
    EXPECT_EQ(p.popup.getCurrentColourForTest(), juce::Colour(255, 0, 255));
    EXPECT_EQ(p.hue->valueText(), "Hue 300 degrees") << "a slider edit moves the hue bar";

    p.sb->keyPressed(key(juce::KeyPress::leftKey, true));
    settle();
    auto* green = sliderNamed(p.popup, "green");
    ASSERT_NE(green, nullptr);
    EXPECT_EQ((int)green->getValue(), p.popup.getCurrentColourForTest().getGreen())
        << "a key on the square moves the sliders";
    EXPECT_GT(green->getValue(), 0.0);
}

TEST(ColourPickerKeyboard, AGreyKeepsTheHueTheUserChose) {
    Picker p(juce::Colours::red);
    p.hue->keyPressed(key(juce::KeyPress::endKey));
    for (int i = 0; i < 12; ++i)
        p.hue->keyPressed(key(juce::KeyPress::leftKey, true));
    const auto hueText = p.hue->valueText();
    p.sb->keyPressed(key(juce::KeyPress::homeKey)); // no saturation: a grey, whose own hue is 0
    settle();
    EXPECT_EQ(p.hue->valueText(), hueText) << "taking the colour out does not throw the hue away";
}

TEST(ColourPickerKeyboard, TheSelectorSlidersTakeFocusAndTheArrowsMoveThemAndPreview) {
    Picker p(juce::Colours::red);
    auto* green = sliderNamed(p.popup, "green");
    ASSERT_NE(green, nullptr);
    EXPECT_TRUE(green->getWantsKeyboardFocus());
    p.popup.setCurrentColourForTest(juce::Colour(10, 100, 10));
    p.previews.clear();
    EXPECT_TRUE(green->keyPressed(key(juce::KeyPress::rightKey)));
    settle();
    EXPECT_EQ(p.popup.getCurrentColourForTest().getGreen(), 101);
    ASSERT_FALSE(p.previews.empty());
    EXPECT_EQ(p.previews.back().getGreen(), 101);
}

TEST(ColourPickerKeyboard, TabVisitsTheSquareTheHueBarTheSlidersTheirValueBoxesThenTheFavourites) {
    Picker p(juce::Colours::red);
    settle(); // the sliders take their keyboard focus once the look and feel has settled
    const auto walk = synth::test::walkTabOrder(p.popup);
    const auto names = walk.names();
    ASSERT_GE(names.size(), 9);
    EXPECT_TRUE(walk.isCompleteCycle());
    const char* expected[] = {
        "Saturation and brightness", "Hue", "Red", "Red value", "Green", "Green value", "Blue", "Blue value",
        "Add to favourites"};
    for (int i = 0; i < 9; ++i)
        EXPECT_EQ(names[i], expected[i]) << "stop " << i;
    EXPECT_TRUE(names[9].startsWith("Favourite colour ")) << names[9];
}

TEST(ColourPickerKeyboard, EveryNewStopHasANameAValueAndATooltipAndTheFocusRingShows) {
    Picker p(juce::Colours::red);
    for (auto* field : {static_cast<ColourPickerField*>(p.sb), static_cast<ColourPickerField*>(p.hue)}) {
        ASSERT_NE(field, nullptr);
        EXPECT_TRUE(field->getWantsKeyboardFocus());
        EXPECT_TRUE(field->getTitle().isNotEmpty());
        EXPECT_TRUE(field->getTooltip().isNotEmpty());
        EXPECT_EQ(field->getDescription(), field->valueText());
        const auto handler = field->createAccessibilityHandler();
        ASSERT_NE(handler, nullptr);
        ASSERT_NE(handler->getValueInterface(), nullptr);
        EXPECT_EQ(handler->getValueInterface()->getCurrentValueAsString(), field->valueText());
    }
    EXPECT_EQ(p.sb->getTitle(), "Saturation and brightness");
    EXPECT_TRUE(p.sb->getTooltip().contains("Left and Right"));
    EXPECT_EQ(p.hue->getTitle(), "Hue");

    const auto ringPixel = [&](bool shown) {
        setFocusRingsShownForTest(shown);
        juce::Image image(juce::Image::ARGB, p.sb->getWidth(), p.sb->getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        p.sb->paintEntireComponent(g, true);
        setFocusRingsShownForTest(false);
        return image.getPixelAt(1, p.sb->getHeight() / 2);
    };
    EXPECT_NE(ringPixel(true), ringPixel(false)) << "the focused square draws the shared focus ring";
}
