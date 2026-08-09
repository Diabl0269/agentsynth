#include "../Source/ShortcutManager.h"
#include <gtest/gtest.h>

class ShortcutManagerTest : public ::testing::Test {
protected:
    ShortcutManager manager;
};

TEST_F(ShortcutManagerTest, DefaultBindingsCorrect) {
    EXPECT_EQ(manager.getBinding("openSettings").getKeyCode(), ',');
    EXPECT_EQ(manager.getBinding("savePreset").getKeyCode(), 's');
    EXPECT_EQ(manager.getBinding("openPreset").getKeyCode(), 'o');
    EXPECT_EQ(manager.getBinding("undo").getKeyCode(), 'z');
    EXPECT_EQ(manager.getBinding("redo").getKeyCode(), 'z');
    EXPECT_TRUE(manager.getBinding("redo").getModifiers().isShiftDown());
}

TEST_F(ShortcutManagerTest, CopyPasteDuplicateUseThePlatformStandardKeys) {
    EXPECT_EQ(manager.getBinding("copySelection").getKeyCode(), 'c');
    EXPECT_TRUE(manager.getBinding("copySelection").getModifiers().isCommandDown());
    EXPECT_FALSE(manager.getBinding("copySelection").getModifiers().isShiftDown());

    EXPECT_EQ(manager.getBinding("pasteSelection").getKeyCode(), 'v');
    EXPECT_TRUE(manager.getBinding("pasteSelection").getModifiers().isCommandDown());

    EXPECT_EQ(manager.getBinding("duplicateSelection").getKeyCode(), 'd');
    EXPECT_TRUE(manager.getBinding("duplicateSelection").getModifiers().isCommandDown());
}

TEST_F(ShortcutManagerTest, EveryDefaultBindingIsUnique) {
    // Adding an action with a binding that is already taken silently shadows one of the two, since
    // getActionForKeyPress returns whichever it reaches first.
    for (const auto& actionId : manager.getActionIds())
        EXPECT_TRUE(manager.getConflictingAction(actionId, manager.getBinding(actionId)).isEmpty())
            << actionId << " collides with " << manager.getConflictingAction(actionId, manager.getBinding(actionId));
}

TEST_F(ShortcutManagerTest, EveryActionIdHasABindingACommandAndADescription) {
    for (const auto& actionId : manager.getActionIds()) {
        EXPECT_NE(manager.getBinding(actionId).getKeyCode(), 0) << actionId << " has no default binding";
        EXPECT_NE(AppCommands::getCommandForAction(actionId), 0) << actionId << " maps to no command";
        EXPECT_NE(ShortcutManager::getActionDescription(actionId), actionId)
            << actionId << " has no human-readable description";
    }
}

TEST_F(ShortcutManagerTest, GetActionForKeyPress_Correct) {
    juce::KeyPress cmdS('s', juce::ModifierKeys::commandModifier, 0);
    EXPECT_EQ(manager.getActionForKeyPress(cmdS), "savePreset");

    juce::KeyPress cmdZ('z', juce::ModifierKeys::commandModifier, 0);
    EXPECT_EQ(manager.getActionForKeyPress(cmdZ), "undo");

    // Cmd+Shift+Z must match redo
    juce::KeyPress cmdShiftZ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    EXPECT_EQ(manager.getActionForKeyPress(cmdShiftZ), "redo");
}

TEST_F(ShortcutManagerTest, GetActionForKeyPress_UnknownReturnsEmpty) {
    juce::KeyPress cmdX('x', juce::ModifierKeys::commandModifier, 0);
    EXPECT_TRUE(manager.getActionForKeyPress(cmdX).isEmpty());
}

TEST_F(ShortcutManagerTest, SetBinding_Updates) {
    juce::KeyPress cmdK('k', juce::ModifierKeys::commandModifier, 0);
    manager.setBinding("openSettings", cmdK);
    EXPECT_EQ(manager.getBinding("openSettings").getKeyCode(), 'k');
    EXPECT_EQ(manager.getActionForKeyPress(cmdK), "openSettings");
}

TEST_F(ShortcutManagerTest, SaveAndLoad_RoundTrips) {
    juce::ApplicationProperties props;
    juce::PropertiesFile::Options opts;
    opts.applicationName = "ShortcutTest";
    opts.folderName = "ShortcutTest";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(opts);

    // Set a custom binding and save
    manager.loadFromProperties(props);
    juce::KeyPress cmdK('k', juce::ModifierKeys::commandModifier, 0);
    manager.setBinding("openSettings", cmdK);
    manager.saveToProperties();

    // Create a new manager and load
    ShortcutManager manager2;
    manager2.loadFromProperties(props);
    EXPECT_EQ(manager2.getBinding("openSettings").getKeyCode(), 'k');

    // Cleanup
    if (auto* settings = props.getUserSettings())
        settings->clear();
}

TEST_F(ShortcutManagerTest, ResetToDefaults_Restores) {
    juce::KeyPress cmdK('k', juce::ModifierKeys::commandModifier, 0);
    manager.setBinding("openSettings", cmdK);
    EXPECT_EQ(manager.getBinding("openSettings").getKeyCode(), 'k');

    manager.resetToDefaults();
    EXPECT_EQ(manager.getBinding("openSettings").getKeyCode(), ',');
}

TEST_F(ShortcutManagerTest, KeyPressToDisplayString_Formats) {
    juce::KeyPress cmdS('s', juce::ModifierKeys::commandModifier, 0);
    auto display = ShortcutManager::keyPressToDisplayString(cmdS);
#if JUCE_MAC
    EXPECT_TRUE(display.contains("Cmd"));
#endif
    EXPECT_TRUE(display.contains("S"));
}

TEST_F(ShortcutManagerTest, GetActionDescription_Works) {
    EXPECT_EQ(ShortcutManager::getActionDescription("openSettings"), "Open Settings");
    EXPECT_EQ(ShortcutManager::getActionDescription("savePreset"), "Save Preset");
    EXPECT_EQ(ShortcutManager::getActionDescription("openPreset"), "Open Preset");
    EXPECT_EQ(ShortcutManager::getActionDescription("undo"), "Undo");
    EXPECT_EQ(ShortcutManager::getActionDescription("redo"), "Redo");
}
