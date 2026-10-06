// KnobStylePickerTests.cpp -- Source/UI/Settings/KnobStylePicker.{h,cpp}. Clicks go through the real
// mouseDown/mouseUp entry points of the style buttons, and the owner persists what onChanged hands it,
// the way AppearanceSettingsTab does.

#include "UI/Settings/KnobStylePicker.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <memory>

namespace {

using namespace synth::theme;

juce::MouseEvent clickEvent(juce::Component& comp, juce::Point<int> pos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), juce::ModifierKeys(),
                            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
}

void click(juce::Component& comp) {
    const auto pos = comp.getLocalBounds().getCentre();
    comp.mouseEnter(clickEvent(comp, pos));
    comp.mouseDown(clickEvent(comp, pos));
    comp.mouseUp(clickEvent(comp, pos));
}

struct PickerRig {
    PickerRig() {
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("KnobPickerRig.settings");
        file.deleteFile();
        juce::PropertiesFile::Options opts;
        opts.applicationName = "KnobPickerRig";
        opts.filenameSuffix = "settings";
        props = std::make_unique<juce::PropertiesFile>(file, opts);
        picker.setTheme(makeObsidian());
        picker.setAppearance(loadKnobAppearance(*props));
        picker.onChanged = [this](const KnobAppearance& a) { writeKnobAppearance(*props, a); };
        picker.setBounds(0, 0, 480, synth::ui::KnobStylePicker::kPreferredHeight);
    }
    std::unique_ptr<juce::PropertiesFile> props;
    synth::ui::KnobStylePicker picker;
};

} // namespace

TEST(KnobStylePickerTest, StartsOnThePersistedStyle) {
    PickerRig rig;
    EXPECT_TRUE(rig.picker.getStyleButtonForTest((int)KnobStyle::Polished).getToggleState());
    EXPECT_TRUE(rig.picker.getFamilyToggleForTest().getToggleState());
}

TEST(KnobStylePickerTest, ClickingAPreviewPersistsItsStyle) {
    PickerRig rig;
    click(rig.picker.getStyleButtonForTest((int)KnobStyle::Hardware));
    EXPECT_EQ(rig.props->getValue(knobStyleKey()), "hardware");
    EXPECT_TRUE(rig.picker.getStyleButtonForTest((int)KnobStyle::Hardware).getToggleState());
    EXPECT_FALSE(rig.picker.getStyleButtonForTest((int)KnobStyle::Polished).getToggleState());
    EXPECT_EQ(rig.picker.getAppearance().style, KnobStyle::Hardware);
}

TEST(KnobStylePickerTest, ClickingTheSwitchPersistsColourByFamily) {
    PickerRig rig;
    click(rig.picker.getFamilyToggleForTest());
    EXPECT_FALSE(rig.props->getBoolValue(knobColourByFamilyKey(), true));
    EXPECT_TRUE(rig.props->containsKey(knobColourByFamilyKey()));
    click(rig.picker.getFamilyToggleForTest());
    EXPECT_TRUE(rig.props->getBoolValue(knobColourByFamilyKey(), false));
}

TEST(KnobStylePickerTest, EachPreviewIsNamedHasATooltipAndTakesFocus) {
    PickerRig rig;
    for (int i = 0; i < kKnobStyleCount; ++i) {
        auto& button = rig.picker.getStyleButtonForTest(i);
        const juce::String label = knobStyleLabel((KnobStyle)i);
        EXPECT_EQ(button.getTitle(), label + " control style");
        EXPECT_EQ(button.getTooltip(), "Use the " + label + " look for every knob and fader");
        EXPECT_TRUE(button.getWantsKeyboardFocus());
    }
    EXPECT_EQ(rig.picker.getFamilyToggleForTest().getTitle(), "Colour controls by module family");
    EXPECT_TRUE(rig.picker.getFamilyToggleForTest().getTooltip().isNotEmpty());
}

TEST(KnobStylePickerTest, ReturnPicksTheFocusedPreview) {
    PickerRig rig;
    juce::Component& button = rig.picker.getStyleButtonForTest((int)KnobStyle::Soft);
    EXPECT_TRUE(button.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // triggerClick() posts the click
    EXPECT_EQ(rig.props->getValue(knobStyleKey()), "soft");
}
