// DepthPickerTests.cpp -- Source/UI/Settings/DepthPicker.{h,cpp} on its own and inside the Appearance tab. Clicks go
// through the switch's real mouseDown entry point and keys through its keyPressed, the paths a user reaches.

#include "../../TestWait.h"
#include "UI/Settings/AppearanceSettingsTab.h"
#include "UI/Settings/DepthPicker.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ThemeManager.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

using namespace synth::theme;
using synth::ui::DepthPicker;

juce::MouseEvent mouseEvent(juce::Component& comp, juce::Point<int> pos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), juce::ModifierKeys(),
                            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
}

// Presses on segment `index` of the picker's switch: down, then up, at the segment's centre.
void clickSegment(DepthPicker& picker, int index) {
    auto& sw = picker.getSwitchForTest();
    const auto pos = sw.getSegment(index)->getBounds().getCentre();
    sw.mouseDown(mouseEvent(sw, pos));
    sw.mouseUp(mouseEvent(sw, pos));
}

bool pressKey(DepthPicker& picker, int keyCode) {
    return picker.getSwitchForTest().keyPressed(juce::KeyPress(keyCode));
}

struct PickerRig {
    PickerRig() {
        picker.setTheme(makeObsidian());
        picker.setBounds(0, 0, 320, DepthPicker::kPreferredHeight);
        picker.onChanged = [this](DepthLevel level) { fired.push_back(level); };
    }
    DepthPicker picker;
    std::vector<DepthLevel> fired;
};

struct IsolatedProps {
    IsolatedProps() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "DepthPickerTabTests";
        opts.folderName = "DepthPickerTabTests";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;
        {
            juce::ApplicationProperties initial;
            initial.setStorageParameters(opts);
            if (auto* s = initial.getUserSettings())
                s->getFile().deleteFile();
        }
        props.setStorageParameters(opts);
    }
    ~IsolatedProps() {
        if (auto* s = props.getUserSettings())
            s->getFile().deleteFile();
    }
    juce::ApplicationProperties props;
};

} // namespace

TEST(DepthPickerTest, StartsOnSoftWithThreeSegmentsInOrder) {
    PickerRig rig;
    auto& sw = rig.picker.getSwitchForTest();
    ASSERT_EQ(sw.getNumSegments(), 3);
    EXPECT_EQ(sw.getSelectedIndex(), (int)DepthLevel::Soft);
    const char* labels[] = {"Flat", "Soft", "Rich"};
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(sw.getSegment(i)->getTitle(), labels[i]);
        EXPECT_GT(sw.getSegment(i)->getWidth(), 0);
    }
    EXPECT_TRUE(sw.getSegment(1)->getToggleState());
}

TEST(DepthPickerTest, ClickingASegmentPicksItAndFiresOnce) {
    PickerRig rig;
    clickSegment(rig.picker, 0);
    ASSERT_EQ(rig.fired.size(), 1u);
    EXPECT_EQ(rig.fired[0], DepthLevel::Flat);
    EXPECT_EQ(rig.picker.getDepthLevel(), DepthLevel::Flat);
    EXPECT_TRUE(rig.picker.getSwitchForTest().getSegment(0)->getToggleState());
    EXPECT_FALSE(rig.picker.getSwitchForTest().getSegment(1)->getToggleState());

    clickSegment(rig.picker, 0); // the level it already has
    EXPECT_EQ(rig.fired.size(), 1u);
}

TEST(DepthPickerTest, LeftRightAndHomeEndMoveAndPickClampedAtTheEnds) {
    PickerRig rig;
    EXPECT_TRUE(pressKey(rig.picker, juce::KeyPress::rightKey));
    EXPECT_EQ(rig.picker.getDepthLevel(), DepthLevel::Rich);
    EXPECT_TRUE(pressKey(rig.picker, juce::KeyPress::rightKey)); // clamped: consumed, no change
    EXPECT_EQ(rig.fired.size(), 1u);
    EXPECT_TRUE(pressKey(rig.picker, juce::KeyPress::leftKey));
    EXPECT_EQ(rig.picker.getDepthLevel(), DepthLevel::Soft);
    EXPECT_TRUE(pressKey(rig.picker, juce::KeyPress::homeKey));
    EXPECT_EQ(rig.picker.getDepthLevel(), DepthLevel::Flat);
    EXPECT_TRUE(pressKey(rig.picker, juce::KeyPress::endKey));
    EXPECT_EQ(rig.picker.getDepthLevel(), DepthLevel::Rich);
    EXPECT_FALSE(pressKey(rig.picker, 'x')) << "other keys are left to the window";
}

TEST(DepthPickerTest, SetDepthLevelMovesTheSelectionWithoutFiring) {
    PickerRig rig;
    rig.picker.setDepthLevel(DepthLevel::Rich);
    EXPECT_EQ(rig.picker.getSwitchForTest().getSelectedIndex(), (int)DepthLevel::Rich);
    EXPECT_TRUE(rig.fired.empty());
}

TEST(DepthPickerTest, IsOneTabStopNamedDepthWithRadioSegmentsAndATooltip) {
    PickerRig rig;
    auto& sw = rig.picker.getSwitchForTest();
    EXPECT_TRUE(sw.getWantsKeyboardFocus());
    EXPECT_EQ(sw.getTitle(), "Depth");
    EXPECT_EQ(sw.getTooltip(), "Flat, Soft or Rich: how much light, shade and lift cards, icons and knobs get");
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(sw.getSegment(i)->getWantsKeyboardFocus()) << "the switch is the one stop";
        EXPECT_NE(sw.getSegment(i)->getRadioGroupId(), 0) << "reads as a radio button";
    }
    const auto handler = sw.createAccessibilityHandler(); // a headless component has no live one
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::group);
    EXPECT_EQ(handler->getTitle(), "Depth");
}

TEST(DepthPickerTest, ReportsItsLevelToTestMode) {
    PickerRig rig;
    rig.picker.setDepthLevel(DepthLevel::Rich);
    juce::DynamicObject state;
    rig.picker.addTestModeState(state);
    EXPECT_EQ(state.getProperty("depth").toString(), "rich");
    EXPECT_EQ((int)state.getProperty("selected"), 2);
    EXPECT_EQ(rig.picker.getComponentID(), "depthPicker");
}

TEST(AppearanceDepthTest, TheTabShowsThePickerAtTheManagersLevelAndAClickChangesIt) {
    IsolatedProps rig;
    ThemeManager manager;
    manager.initialise(&rig.props);
    manager.setDepthLevel(DepthLevel::Flat);
    AppearanceSettingsTab tab(manager, rig.props);
    tab.setSize(500, 420);

    auto& picker = tab.getDepthPickerForTest();
    EXPECT_EQ(picker.getDepthLevel(), DepthLevel::Flat) << "reflects the manager";
    EXPECT_TRUE(tab.isShowing() || picker.isVisible());
    EXPECT_FALSE(picker.getBounds().isEmpty());
    EXPECT_LE(picker.getBottom(), tab.getContentHeightForTest()) << "inside the scrollable content";

    clickSegment(picker, 2);
    EXPECT_EQ(manager.getDepthLevel(), DepthLevel::Rich);
    EXPECT_EQ(manager.getActiveTheme().treatment.depth, DepthLevel::Rich);
    EXPECT_EQ(rig.props.getUserSettings()->getValue(depthLevelKey()), "rich");
}

TEST(AppearanceDepthTest, ADepthChangeMadeElsewhereMovesThePicker) {
    IsolatedProps rig;
    ThemeManager manager;
    manager.initialise(&rig.props);
    AppearanceSettingsTab tab(manager, rig.props);
    manager.setDepthLevel(DepthLevel::Rich);
    synth::test::waitUntil([&] { return tab.getDepthPickerForTest().getDepthLevel() == DepthLevel::Rich; });
    EXPECT_EQ(tab.getDepthPickerForTest().getDepthLevel(), DepthLevel::Rich);
}
