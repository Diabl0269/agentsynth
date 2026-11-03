// TimelinePanelClipSizingTests.cpp — the edit-tool strip's Sizing field: the menu, the lit state, the Select key
// pressed again, the user setting and what reaches the clip lanes.

#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ClipSizingMode;

namespace {

struct SizingPanelFixture {
    juce::ApplicationProperties props;
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;

    SizingPanelFixture() {
        juce::PropertiesFile::Options options;
        options.applicationName = "TimelinePanelClipSizingTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters(options);
        props.getUserSettings()->clear();
        panel.setSize(1200, 320);
        panel.setTimelineDoc(&doc);
        panel.setApplicationProperties(&props);
    }
    ~SizingPanelFixture() { props.getUserSettings()->clear(); }

    synth::ui::ClipSizingButton& field() { return panel.getClipSizingButton(); }
};

} // namespace

TEST(TimelineClipSizingFieldTest, StartsOnTrimUnlitAndNamed) {
    SizingPanelFixture f;
    EXPECT_EQ(f.field().getMode(), ClipSizingMode::Trim);
    EXPECT_FALSE(f.field().getToggleState());
    EXPECT_EQ(f.field().getButtonText(), "Sizing: Trim");
    EXPECT_EQ(f.field().getTitle(), "Clip sizing, Trim");
    EXPECT_TRUE(f.field().getTooltip().contains("Option")) << f.field().getTooltip();
    EXPECT_TRUE(f.field().isVisible());
    EXPECT_FALSE(f.field().getBounds().isEmpty()) << "laid out on the strip";
}

TEST(TimelineClipSizingFieldTest, TheMenuListsTheThreeModesAndPickingOneReachesTheLanesLitAndSaved) {
    SizingPanelFixture f;
    juce::PopupMenu captured;
    f.field().showMenuHook = [&captured](juce::PopupMenu& menu) { captured = menu; };
    f.field().clicked(); // what a click on the field runs

    juce::StringArray items;
    for (juce::PopupMenu::MenuItemIterator it(captured); it.next();)
        if (!it.getItem().isSeparator)
            items.add(it.getItem().text);
    ASSERT_EQ(items.size(), 4) << items.joinIntoString(" | ");
    EXPECT_TRUE(items[0].startsWith("Trim"));
    EXPECT_TRUE(items[1].startsWith("Carry contents"));
    EXPECT_TRUE(items[2].startsWith("Stretch to fit"));
    EXPECT_TRUE(items[2].contains("notes and audio")) << "audio clips stretch too";
    EXPECT_FALSE(items[2].contains("new length")) << "one short clause per mode";
    EXPECT_TRUE(items[3].contains("Option"));

    f.field().choose(ClipSizingMode::Stretch);
    EXPECT_EQ(f.panel.getClipLaneArea().getClipSizingMode(), ClipSizingMode::Stretch);
    EXPECT_TRUE(f.field().getToggleState()) << "lit while it is not Trim";
    EXPECT_EQ(f.field().getTitle(), "Clip sizing, Stretch to fit");
    EXPECT_EQ(f.props.getUserSettings()->getValue("timelineClipSizingMode"), "stretch");
}

// Regression test for FRO1407: the menu opened far wider than its words needed, because every row carried a long
// "name: description" sentence and the menu takes the width of its longest row. JUCE sizes a menu from each row's
// getIdealPopupMenuItemSize, so the cap is measured the same way, with the app's look-and-feel.
TEST(TimelineClipSizingFieldTest, TheMenuStaysNarrowAndTheModesKeepTheirFullNames) {
    SizingPanelFixture f;
    synth::theme::AppLookAndFeel lnf;
    const auto menu = f.field().buildMenu();
    constexpr int kMaxRowChars = 40;
    constexpr int kMaxRowWidthPx = 340; // a long row used to be about 470 px
    int widest = 0;
    juce::String widestText;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
        const auto& item = it.getItem();
        if (item.isSeparator)
            continue;
        int width = 0, height = 0;
        lnf.getIdealPopupMenuItemSize(item.text, false, -1, width, height);
        EXPECT_LE(item.text.length(), kMaxRowChars) << item.text;
        if (width > widest) {
            widest = width;
            widestText = item.text;
        }
    }
    EXPECT_LE(widest, kMaxRowWidthPx) << "widest row: " << widestText;

    for (const auto& [mode, name] : {std::pair{ClipSizingMode::Trim, "Clip sizing, Trim"},
                                     std::pair{ClipSizingMode::CarryContents, "Clip sizing, Carry contents"},
                                     std::pair{ClipSizingMode::Stretch, "Clip sizing, Stretch to fit"}}) {
        f.field().choose(mode);
        EXPECT_EQ(f.field().getTitle(), name) << "the screen-reader name keeps the full mode name";
    }
}

TEST(TimelineClipSizingFieldTest, TheSavedModeIsRestoredIntoAFreshPanel) {
    SizingPanelFixture f;
    f.field().choose(ClipSizingMode::CarryContents);

    synth::ui::TimelinePanelComponent fresh;
    fresh.setApplicationProperties(&f.props);
    EXPECT_EQ(fresh.getClipSizingButton().getMode(), ClipSizingMode::CarryContents);
    EXPECT_EQ(fresh.getClipLaneArea().getClipSizingMode(), ClipSizingMode::CarryContents);
}

TEST(TimelineClipSizingFieldTest, SelectKeyAgainStepsTheModeButFromAnotherToolPicksSelect) {
    SizingPanelFixture f;
    ASSERT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Select);
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('1')));
    EXPECT_EQ(f.field().getMode(), ClipSizingMode::CarryContents);
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('1')));
    EXPECT_EQ(f.field().getMode(), ClipSizingMode::Stretch);
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('1')));
    EXPECT_EQ(f.field().getMode(), ClipSizingMode::Trim);

    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('3')));
    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('1')));
    EXPECT_EQ(f.panel.getActiveTool(), synth::ui::EditTool::Select);
    EXPECT_EQ(f.field().getMode(), ClipSizingMode::Trim) << "the first press only picks the tool";
}

TEST(TimelineClipSizingFieldTest, SelectKeyAgainFollowsARebind) {
    SizingPanelFixture f;
    ShortcutManager shortcuts;
    f.panel.setShortcutManager(&shortcuts);
    shortcuts.setBinding("timelineToolSelect", juce::KeyPress('v', juce::ModifierKeys::noModifiers, 0));

    EXPECT_TRUE(f.panel.keyPressed(juce::KeyPress('v', juce::ModifierKeys::noModifiers, 0)));
    EXPECT_EQ(f.field().getMode(), ClipSizingMode::CarryContents);
    f.panel.keyPressed(juce::KeyPress('1'));
    EXPECT_EQ(f.field().getMode(), ClipSizingMode::CarryContents) << "the old digit no longer steps it";
    f.panel.setShortcutManager(nullptr);
}
