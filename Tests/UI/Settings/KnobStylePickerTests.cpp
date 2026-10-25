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
    PickerRig rig; // nothing saved yet: the default, Classic
    EXPECT_TRUE(rig.picker.getStyleButtonForTest((int)KnobStyle::Classic).getToggleState());
    EXPECT_TRUE(rig.picker.getFamilyToggleForTest().getToggleState());
}

TEST(KnobStylePickerTest, ClickingAPreviewPersistsItsStyle) {
    PickerRig rig;
    click(rig.picker.getStyleButtonForTest((int)KnobStyle::Hardware));
    EXPECT_EQ(rig.props->getValue(knobStyleKey()), "hardware");
    EXPECT_TRUE(rig.picker.getStyleButtonForTest((int)KnobStyle::Hardware).getToggleState());
    EXPECT_FALSE(rig.picker.getStyleButtonForTest((int)KnobStyle::Classic).getToggleState());
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
        EXPECT_EQ(button.getTitle(), label + " knob style");
        EXPECT_EQ(button.getTooltip(), "Use the " + label + " look for every knob");
        EXPECT_TRUE(button.getWantsKeyboardFocus());
        auto& fader = rig.picker.getFaderStyleButtonForTest(i);
        EXPECT_EQ(fader.getTitle(), label + " fader style");
        EXPECT_EQ(fader.getTooltip(), "Use the " + label + " look for every fader");
        EXPECT_TRUE(fader.getWantsKeyboardFocus());
    }
    EXPECT_EQ(rig.picker.getFamilyToggleForTest().getTitle(), "Colour controls by module family");
    EXPECT_TRUE(rig.picker.getFamilyToggleForTest().getTooltip().isNotEmpty());
}

TEST(KnobStylePickerTest, ReturnPicksTheFocusedPreview) {
    PickerRig rig;
    juce::Component& button = rig.picker.getStyleButtonForTest((int)KnobStyle::Analog);
    EXPECT_TRUE(button.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // triggerClick() posts the click
    EXPECT_EQ(rig.props->getValue(knobStyleKey()), "analog");
}

TEST(KnobStylePickerTest, OffersTheFiveStylesInOrder) {
    PickerRig rig;
    ASSERT_EQ(kKnobStyleCount, 5);
    const char* names[] = {"Classic", "Chunky", "Analog", "Neon", "Ring"};
    for (int i = 0; i < kKnobStyleCount; ++i) {
        auto& button = rig.picker.getStyleButtonForTest(i);
        EXPECT_EQ(button.getTitle(), juce::String(names[i]) + " knob style");
        EXPECT_EQ(rig.picker.getFaderStyleButtonForTest(i).getTitle(), juce::String(names[i]) + " fader style");
        EXPECT_TRUE(button.isVisible());
        EXPECT_GT(button.getWidth(), 0);
    }
}

TEST(KnobStylePickerTest, ARetiredSavedStyleSelectsItsSuccessor) {
    PickerRig rig;
    rig.props->setValue(knobStyleKey(), "polished");
    rig.picker.setAppearance(loadKnobAppearance(*rig.props));
    EXPECT_TRUE(rig.picker.getStyleButtonForTest((int)KnobStyle::Analog).getToggleState());
    rig.props->setValue(knobStyleKey(), "soft");
    rig.picker.setAppearance(loadKnobAppearance(*rig.props));
    EXPECT_TRUE(rig.picker.getStyleButtonForTest((int)KnobStyle::Hardware).getToggleState());
}

TEST(KnobStylePickerTest, ClickingAFaderPreviewChangesOnlyTheFaderStyle) {
    PickerRig rig;
    int fired = 0;
    rig.picker.onChanged = [&](const KnobAppearance& a) {
        ++fired;
        writeKnobAppearance(*rig.props, a);
    };
    click(rig.picker.getFaderStyleButtonForTest((int)KnobStyle::Neon));
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(rig.picker.getAppearance().faderStyle, KnobStyle::Neon);
    EXPECT_EQ(rig.picker.getAppearance().style, KnobStyle::Classic);
    EXPECT_EQ(rig.props->getValue(faderStyleKey()), "neon");
    EXPECT_TRUE(rig.picker.getFaderStyleButtonForTest((int)KnobStyle::Neon).getToggleState());
    EXPECT_TRUE(rig.picker.getStyleButtonForTest((int)KnobStyle::Classic).getToggleState());
}

TEST(KnobStylePickerTest, ClickingAKnobPreviewChangesOnlyTheKnobStyle) {
    PickerRig rig;
    click(rig.picker.getFaderStyleButtonForTest((int)KnobStyle::Ring));
    click(rig.picker.getStyleButtonForTest((int)KnobStyle::Analog));
    EXPECT_EQ(rig.picker.getAppearance().style, KnobStyle::Analog);
    EXPECT_EQ(rig.picker.getAppearance().faderStyle, KnobStyle::Ring);
    EXPECT_TRUE(rig.picker.getFaderStyleButtonForTest((int)KnobStyle::Ring).getToggleState());
}

TEST(KnobStylePickerTest, SetAppearanceTogglesTheRightButtonInEachRow) {
    PickerRig rig;
    rig.picker.setAppearance({KnobStyle::Hardware, true, KnobStyle::Ring});
    for (int i = 0; i < kKnobStyleCount; ++i) {
        EXPECT_EQ(rig.picker.getStyleButtonForTest(i).getToggleState(), i == (int)KnobStyle::Hardware) << i;
        EXPECT_EQ(rig.picker.getFaderStyleButtonForTest(i).getToggleState(), i == (int)KnobStyle::Ring) << i;
    }
}

TEST(KnobStylePickerTest, ArrowKeysMoveWithinTheFaderRowOnly) {
    PickerRig rig;
    auto& window = rig.picker;
    juce::Component parent;
    parent.setBounds(0, 0, 480, synth::ui::KnobStylePicker::kPreferredHeight);
    parent.addAndMakeVisible(window);
    parent.setVisible(true);
    parent.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    juce::Component& second = window.getFaderStyleButtonForTest(1);
    second.grabKeyboardFocus();
    if (!second.hasKeyboardFocus(false))
        GTEST_SKIP() << "no keyboard focus in this headless session";
    EXPECT_TRUE(second.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_TRUE(window.getFaderStyleButtonForTest(2).hasKeyboardFocus(false));
    EXPECT_FALSE(window.getStyleButtonForTest(2).hasKeyboardFocus(false));
    juce::Component& last = window.getFaderStyleButtonForTest(kKnobStyleCount - 1);
    last.grabKeyboardFocus();
    EXPECT_TRUE(last.keyPressed(juce::KeyPress(juce::KeyPress::rightKey))); // clamped at the end
    EXPECT_TRUE(last.hasKeyboardFocus(false));
    parent.removeFromDesktop();
}
