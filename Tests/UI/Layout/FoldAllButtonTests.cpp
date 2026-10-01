// FoldAllButtonTests.cpp -- the one "Collapse all" / "Expand all" strip button shared by the Keyboard
// Shortcuts tab and the Preferences All view.
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/FoldAllButton.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include "UI/Settings/ShortcutsSettingsTab.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace {
using synth::ui::FoldAllButton;

std::vector<FoldAllButton*> foldAllButtonsUnder(juce::Component& root) {
    std::vector<FoldAllButton*> out;
    std::vector<juce::Component*> stack{&root};
    while (!stack.empty()) {
        auto* c = stack.back();
        stack.pop_back();
        if (auto* button = dynamic_cast<FoldAllButton*>(c))
            out.push_back(button);
        for (auto* child : c->getChildren())
            stack.push_back(child);
    }
    return out;
}
} // namespace

TEST(FoldAllButtonTest, ReadsCollapseAllUntilEverythingIsFoldedThenExpandAll) {
    FoldAllButton button;
    EXPECT_EQ(button.getButtonText(), "Collapse all");
    EXPECT_EQ(button.getTooltip(), "Fold every section");
    EXPECT_FALSE(button.isAllFolded());

    button.setAllFolded(true);
    EXPECT_EQ(button.getButtonText(), "Expand all");
    EXPECT_EQ(button.getTooltip(), "Unfold every section");
    EXPECT_TRUE(button.isAllFolded());

    button.setAllFolded(true); // idempotent
    EXPECT_EQ(button.getButtonText(), "Expand all");
    button.setAllFolded(false);
    EXPECT_EQ(button.getButtonText(), "Collapse all");
    EXPECT_EQ(button.getTooltip(), "Fold every section");
}

TEST(FoldAllButtonTest, IsKeyboardReachableAndPaintsInBothStates) {
    FoldAllButton button;
    button.setSize(300, FoldAllButton::kStripHeight);
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    for (bool folded : {false, true}) {
        button.setAllFolded(folded);
        juce::Image image(juce::Image::ARGB, 300, FoldAllButton::kStripHeight, true);
        juce::Graphics g(image);
        EXPECT_NO_THROW(button.paintEntireComponent(g, false));
    }
}

TEST(FoldAllButtonTest, TheShortcutsTabHasExactlyOneAndItFlipsWithTheFolds) {
    ShortcutManager manager;
    ShortcutsSettingsTab tab(manager);
    tab.setSize(600, 500);
    const auto buttons = foldAllButtonsUnder(tab);
    ASSERT_EQ(buttons.size(), 1u);
    EXPECT_TRUE(buttons.front()->isVisible());
    EXPECT_EQ(buttons.front()->getButtonText(), "Collapse all");

    buttons.front()->onClick();
    EXPECT_TRUE(tab.areAllSectionsCollapsed());
    EXPECT_EQ(buttons.front()->getButtonText(), "Expand all");
    buttons.front()->onClick();
    EXPECT_FALSE(tab.areAllSectionsCollapsed());
    EXPECT_EQ(buttons.front()->getButtonText(), "Collapse all");
}

TEST(FoldAllButtonTest, ThePreferencesAllViewHasExactlyOneOfTheSameClassAndItFlipsWithTheFolds) {
    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "FoldAllButtonTest";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);
    props.getUserSettings()->clear();

    PreferencesSettingsTab tab(props);
    tab.setSize(520, 700);
    tab.setSelectedCategory(PreferencesSettingsTab::Category::All);
    const auto buttons = foldAllButtonsUnder(tab);
    ASSERT_EQ(buttons.size(), 1u);
    EXPECT_EQ(buttons.front(), &tab.getFoldAllButtonForTest());
    EXPECT_TRUE(buttons.front()->isVisible());

    buttons.front()->onClick();
    EXPECT_TRUE(tab.areAllSectionsCollapsed());
    EXPECT_EQ(buttons.front()->getButtonText(), "Expand all");
    buttons.front()->onClick();
    EXPECT_EQ(buttons.front()->getButtonText(), "Collapse all");
    props.getUserSettings()->clear();
}
