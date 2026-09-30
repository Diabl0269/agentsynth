#include "PreferencesSettingsTabTestFixture.h"

// Topic: the "Patch save location" row in Files & Autosave.

namespace {
juce::ComboBox* findPatchSaveCombo(PreferencesSettingsTab& tab) {
    for (auto* child : descendantsOf(tab))
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child))
            if (combo->getNumItems() == 3 && combo->getItemText(0) == "With each project")
                return combo;
    return nullptr;
}
} // namespace

TEST_F(PreferencesSettingsTabTest, PatchSaveLocationDefaultsToPerProjectAndDoesNotWriteWhenUntouched) {
    PreferencesSettingsTab tab(appProperties);
    EXPECT_EQ(tab.getPatchSaveMode(), synth::PatchSaveMode::PerProject);
    EXPECT_EQ(tab.getPatchSaveCustomFolder(), juce::File());
    EXPECT_FALSE(appProperties.getUserSettings()->containsKey("patchSaveCustomDir"));
}

TEST_F(PreferencesSettingsTabTest, PatchSaveLocationRoundTripsAcrossReopen) {
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agsynth-fro8");
    {
        PreferencesSettingsTab tab(appProperties);
        tab.setPatchSaveMode(synth::PatchSaveMode::Custom);
        tab.setPatchSaveCustomFolder(folder);
        EXPECT_EQ(appProperties.getUserSettings()->getValue("patchSaveMode"), "custom");
    }
    PreferencesSettingsTab reopened(appProperties);
    EXPECT_EQ(reopened.getPatchSaveMode(), synth::PatchSaveMode::Custom);
    EXPECT_EQ(reopened.getPatchSaveCustomFolder(), folder);

    reopened.setPatchSaveMode(synth::PatchSaveMode::Global);
    PreferencesSettingsTab again(appProperties);
    EXPECT_EQ(again.getPatchSaveMode(), synth::PatchSaveMode::Global);
}

// The row is reached the way a user reaches it: pick Files & Autosave in the category combo, then
// drive the row's own combo through its change path.
TEST_F(PreferencesSettingsTabTest, PatchSaveRowAppearsInFilesCategoryAndComboPersists) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 700);
    auto* combo = findPatchSaveCombo(tab);
    ASSERT_NE(combo, nullptr);
    EXPECT_FALSE(combo->isVisible()) << "opens on Graph";

    tab.getCategoryComboForTest().setSelectedId(static_cast<int>(PreferencesSettingsTab::Category::Files) + 1,
                                                juce::sendNotificationSync);
    EXPECT_TRUE(combo->isVisible());

    combo->setSelectedItemIndex(1, juce::sendNotificationSync); // one shared folder
    EXPECT_EQ(appProperties.getUserSettings()->getValue("patchSaveMode"), "global");
    EXPECT_EQ(tab.getPatchSaveMode(), synth::PatchSaveMode::Global);
    combo->setSelectedItemIndex(2, juce::sendNotificationSync); // a folder I choose
    EXPECT_EQ(appProperties.getUserSettings()->getValue("patchSaveMode"), "custom");
}
