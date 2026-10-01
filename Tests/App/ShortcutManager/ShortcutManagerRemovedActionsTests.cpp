// Concern: the tabPrevious / tabNext actions are gone (a tab strip answers its own arrow keys), and a
// settings file written while they existed still loads: the stale keys are ignored, never asserted on.
#include "ShortcutManager/AppCommands.h"
#include "ShortcutManagerTestFixture.h"
#include <gtest/gtest.h>

namespace {
constexpr int kCmdAlt = juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier;
}

TEST_F(ShortcutManagerTest, TheTabStepActionsAreNoLongerRegistered) {
    for (const auto* id : {"tabPrevious", "tabNext"}) {
        EXPECT_FALSE(manager.getActionIds().contains(id)) << id;
        EXPECT_EQ(AppCommands::getCommandForAction(id), AppCommands::kNoCommand) << id;
        EXPECT_NE(ShortcutManager::getActionDescription(id), "Previous Tab") << id;
        EXPECT_NE(ShortcutManager::getActionDescription(id), "Next Tab") << id;
    }
    for (int arrow : {juce::KeyPress::leftKey, juce::KeyPress::rightKey})
        EXPECT_TRUE(manager.getActionsForKeyPress(juce::KeyPress(arrow, juce::ModifierKeys(kCmdAlt), 0)).isEmpty())
            << "Cmd+Option+arrow is free";
}

TEST_F(ShortcutManagerTest, ASavedBindingForARemovedTabActionIsIgnoredOnLoad) {
    juce::ApplicationProperties props;
    juce::PropertiesFile::Options opts;
    opts.applicationName = "ShortcutRemovedActionTest";
    opts.folderName = "ShortcutRemovedActionTest";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(opts);
    auto* settings = props.getUserSettings();
    ASSERT_NE(settings, nullptr);
    settings->clear();

    // A file saved by an older build: every action's key, including the two that no longer exist.
    ShortcutManager writer;
    writer.loadFromProperties(props);
    writer.setBinding("openSettings", juce::KeyPress('k', juce::ModifierKeys::commandModifier, 0));
    writer.saveToProperties();
    settings->setValue("shortcut_tabNext", settings->getValue("shortcut_openSettings"));
    settings->setValue("shortcut_tabPrevious", settings->getValue("shortcut_openSettings"));

    ShortcutManager loaded;
    EXPECT_NO_FATAL_FAILURE(loaded.loadFromProperties(props));
    EXPECT_EQ(loaded.getBinding("openSettings").getKeyCode(), 'k') << "the rest of the file still loads";
    EXPECT_FALSE(loaded.getBinding("tabNext").isValid());
    EXPECT_FALSE(loaded.getBinding("tabPrevious").isValid());
    EXPECT_NO_FATAL_FAILURE(loaded.saveToProperties());
    settings->clear();
}
