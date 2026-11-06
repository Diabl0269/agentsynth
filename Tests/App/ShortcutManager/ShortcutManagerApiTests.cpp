// Concern: ShortcutsSettingsTab's pure filter helpers, the basic ShortcutManager API surface,
// the shortcutHintFor tooltip helper, and the ChangeBroadcaster/persistence broadcast contract.
#include "ShortcutManagerTestFixture.h"
#include "UI/Settings/ShortcutsSettingsTab.h"

// ---------------------------------------------------------------------------
// ShortcutsSettingsTab pure helpers (no GUI needed)
// ---------------------------------------------------------------------------

TEST(ShortcutsSettingsFilterTest, RowMatchesDescriptionAndBindingTextCaseInsensitively) {
    // No query hides nothing — this answers "is the row visible", not "is this a highlight hit".
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("", "Undo", "Cmd + Z"));
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("   ", "Undo", "Cmd + Z")) << "whitespace is not a filter";

    // Description, either case, and as a substring.
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("undo", "Undo", "Cmd + Z"));
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("semitone transpose", "Transpose Up a Semitone", "Up"))
        << "words in any order";
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("cmd undo", "Undo", "Cmd + Z"))
        << "across description and binding";
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("UND", "Undo", "Cmd + Z"));
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("ose up", "Transpose Up a Semitone", "Up"));

    // Binding text — the commonest question is "what is on Shift+Q?", which description-only search
    // cannot answer.
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("cmd", "Undo", "Cmd + Z"));
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("shift", "Quantise Selected Notes", "Shift + Q"));
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery("left", "Grid Coarser", "Ctrl + Shift + Left"));

    EXPECT_FALSE(ShortcutsSettingsTab::rowMatchesQuery("reverb", "Undo", "Cmd + Z"));
}

TEST(ShortcutsSettingsFilterTest, SectionVisibilityAndAutoExpandUnderAFilter) {
    // No filter: visibility is unconditional, and the fold is the user's own collapse flag.
    EXPECT_TRUE(ShortcutsSettingsTab::sectionIsVisible(/*filterActive=*/false, /*sectionHasMatch=*/false));
    EXPECT_TRUE(ShortcutsSettingsTab::sectionIsExpanded(false, false, /*collapsed=*/false));
    EXPECT_FALSE(ShortcutsSettingsTab::sectionIsExpanded(false, true, /*collapsed=*/true));

    // Filter active: a section with no matches disappears header and all, rather than leaving a
    // lone header over empty space.
    EXPECT_FALSE(ShortcutsSettingsTab::sectionIsVisible(true, false));
    EXPECT_TRUE(ShortcutsSettingsTab::sectionIsVisible(true, true));

    // A matching section is FORCED open — including one the user had folded, so a match can never be
    // trapped inside a fold. The collapse flag itself is untouched, which is what lets clearing the
    // query restore exactly the folds they had.
    EXPECT_TRUE(ShortcutsSettingsTab::sectionIsExpanded(true, true, /*collapsed=*/true));
    EXPECT_TRUE(ShortcutsSettingsTab::sectionIsExpanded(true, true, /*collapsed=*/false));
    EXPECT_FALSE(ShortcutsSettingsTab::sectionIsExpanded(true, false, /*collapsed=*/false));
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
    // Cmd+Y has no default binding (Cmd+X became cutSelection and Cmd+J the bottom panel, so neither works as
    // the "unknown" sample here).
    juce::KeyPress cmdY('y', juce::ModifierKeys::commandModifier, 0);
    EXPECT_TRUE(manager.getActionForKeyPress(cmdY).isEmpty());
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

// Installs that ever saved their settings persist EVERY action's key, so the Cmd+Shift+S <-> Cmd+Opt+S
// swap between Save Project As and Save Snippet has to be migrated on load, once, and only for a
// pair that still holds the old defaults.
TEST_F(ShortcutManagerTest, LoadMigratesOldSaveAsAndSnippetChordsOnce) {
    juce::ApplicationProperties props;
    juce::PropertiesFile::Options opts;
    opts.applicationName = "ShortcutMigrationTest";
    opts.folderName = "ShortcutMigrationTest";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(opts);
    auto* settings = props.getUserSettings();
    ASSERT_NE(settings, nullptr);
    settings->clear();

    const juce::KeyPress cmdShiftS('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    const juce::KeyPress cmdOptS('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0);
    settings->setValue("shortcut_saveSnippet", ShortcutManager::encodeKeyPress(cmdShiftS));
    settings->setValue("shortcut_saveProjectAs", ShortcutManager::encodeKeyPress(cmdOptS));

    ShortcutManager migrated;
    migrated.loadFromProperties(props);
    EXPECT_EQ(migrated.getBinding("saveProjectAs"), cmdShiftS);
    EXPECT_EQ(migrated.getBinding("saveSnippet"), cmdOptS);

    // Once only: a user who deliberately rebinds back to the old pair is left alone.
    settings->setValue("shortcut_saveSnippet", ShortcutManager::encodeKeyPress(cmdShiftS));
    settings->setValue("shortcut_saveProjectAs", ShortcutManager::encodeKeyPress(cmdOptS));
    ShortcutManager again;
    again.loadFromProperties(props);
    EXPECT_EQ(again.getBinding("saveProjectAs"), cmdOptS);
    EXPECT_EQ(again.getBinding("saveSnippet"), cmdShiftS);

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
    EXPECT_EQ(ShortcutManager::getActionDescription("savePreset"), "Save Project");
    EXPECT_EQ(ShortcutManager::getActionDescription("openProject"), "Open Project");
    EXPECT_EQ(ShortcutManager::getActionDescription("undo"), "Undo");
    EXPECT_EQ(ShortcutManager::getActionDescription("redo"), "Redo");
}

// ---------------------------------------------------------------------------
// shortcutHintFor — the shared tooltip-hint helper every dynamic shortcut hint routes through.
// ---------------------------------------------------------------------------

TEST_F(ShortcutManagerTest, ShortcutHintForRendersABareLetterLowercase) {
    manager.setBinding("timelineSnapToggle", juce::KeyPress('q', juce::ModifierKeys::noModifiers, 0));
    EXPECT_EQ(shortcutHintFor(&manager, "timelineSnapToggle", juce::KeyPress()), "q");
}

TEST_F(ShortcutManagerTest, ShortcutHintForRendersAModifierComboAsKeyPressToDisplayStringSpellsIt) {
    manager.setBinding("pianoRollToggleScalePanel", juce::KeyPress('s', juce::ModifierKeys::ctrlModifier, 0));
    const auto hint = shortcutHintFor(&manager, "pianoRollToggleScalePanel", juce::KeyPress());
    EXPECT_EQ(hint, ShortcutManager::keyPressToDisplayString(manager.getBinding("pianoRollToggleScalePanel")));
    EXPECT_TRUE(hint.contains("S")) << "a chorded letter is NOT lower-cased, only a bare one is";
}

TEST_F(ShortcutManagerTest, ShortcutHintForAClearedBindingIsEmptyRatherThanTheFallback) {
    manager.setBinding("timelineFollowPlayheadToggle", juce::KeyPress());
    EXPECT_EQ(shortcutHintFor(&manager, "timelineFollowPlayheadToggle",
                              juce::KeyPress('f', juce::ModifierKeys::noModifiers, 0)),
              juce::String())
        << "an installed manager is the ONLY source once one exists -- a cleared binding has no key";
}

TEST_F(ShortcutManagerTest, ShortcutHintForWithNoManagerUsesTheFallback) {
    EXPECT_EQ(shortcutHintFor(nullptr, "timelineFollowPlayheadToggle",
                              juce::KeyPress('f', juce::ModifierKeys::noModifiers, 0)),
              "f");
    EXPECT_EQ(shortcutHintFor(nullptr, "anything", juce::KeyPress()), juce::String())
        << "an invalid fallback is also a real 'no key' state";
}

// ---------------------------------------------------------------------------
// ChangeBroadcaster — additive alongside the pre-existing single-slot onBindingsChanged, so
// MULTIPLE surfaces (not just MainComponent) can react to a rebind.
// ---------------------------------------------------------------------------

TEST_F(ShortcutManagerTest, SaveToPropertiesBroadcastsAChangeEvenWithNoPropertiesFileWired) {
    struct CountingListener : juce::ChangeListener {
        int count = 0;
        void changeListenerCallback(juce::ChangeBroadcaster*) override { ++count; }
    } listener;
    manager.addChangeListener(&listener);

    // The mutation itself broadcasts (see setBinding) ...
    manager.setBinding("timelineFollowPlayheadToggle", juce::KeyPress('g', juce::ModifierKeys::noModifiers, 0));
    EXPECT_EQ(listener.count, 1);
    // ... and so does the save, even with no ApplicationProperties ever wired — the documented
    // (idempotent) double-fire on the Settings tab's rebind path.
    manager.saveToProperties();
    EXPECT_EQ(listener.count, 2);

    manager.removeChangeListener(&listener);
}

TEST_F(ShortcutManagerTest, OnBindingsChangedSingleSlotStillFiresAlongsideTheBroadcaster) {
    int legacyCallbackCount = 0;
    manager.onBindingsChanged = [&] { ++legacyCallbackCount; };
    struct CountingListener : juce::ChangeListener {
        int count = 0;
        void changeListenerCallback(juce::ChangeBroadcaster*) override { ++count; }
    } listener;
    manager.addChangeListener(&listener);

    manager.saveToProperties();
    EXPECT_EQ(legacyCallbackCount, 1)
        << "the pre-existing single-slot callback (MainComponent's own) must be untouched";
    EXPECT_EQ(listener.count, 1);

    manager.removeChangeListener(&listener);
}

// ---------------------------------------------------------------------------
// keyPressToDisplayString names the keys that are not characters
// ---------------------------------------------------------------------------

// Regression test for FRO1202: Return, Escape, Tab, Backspace, Delete, Home, End, Page Up/Down and F1..F12 printed a
// control character, so a hint read 'Alt + )' and the Settings search found nothing by the key's name.
TEST(ShortcutDisplayNamesTest, NamedKeysReadAsWordsNotControlCharacters) {
    using juce::KeyPress;
    const std::pair<int, const char*> named[] = {
        {KeyPress::returnKey, "Return"},
        {KeyPress::escapeKey, "Escape"},
        {KeyPress::tabKey, "Tab"},
        {KeyPress::backspaceKey, "Backspace"},
        {KeyPress::deleteKey, "Delete"},
        {KeyPress::homeKey, "Home"},
        {KeyPress::endKey, "End"},
        {KeyPress::pageUpKey, "Page Up"},
        {KeyPress::pageDownKey, "Page Down"},
        {KeyPress::F1Key, "F1"},
        {KeyPress::F2Key, "F2"},
        {KeyPress::F3Key, "F3"},
        {KeyPress::F4Key, "F4"},
        {KeyPress::F5Key, "F5"},
        {KeyPress::F6Key, "F6"},
        {KeyPress::F7Key, "F7"},
        {KeyPress::F8Key, "F8"},
        {KeyPress::F9Key, "F9"},
        {KeyPress::F10Key, "F10"},
        {KeyPress::F11Key, "F11"},
        {KeyPress::F12Key, "F12"},
    };
    for (const auto& [code, name] : named) {
        EXPECT_EQ(ShortcutManager::keyPressToDisplayString(KeyPress(code)), name);
        EXPECT_EQ(ShortcutManager::keyPressToDisplayString(KeyPress(code, juce::ModifierKeys::altModifier, 0)),
                  juce::String("Alt + ") + name);
    }
    EXPECT_EQ(
        ShortcutManager::keyPressToDisplayString(KeyPress(KeyPress::returnKey, juce::ModifierKeys::shiftModifier, 0)),
        "Shift + Return");
}

// Every default binding is spelled with printable characters only, and a search by the key's name finds its row.
TEST_F(ShortcutManagerTest, EveryDefaultBindingHasAPrintableDisplayName) {
    for (const auto& id : manager.getActionIds()) {
        const auto key = manager.getBinding(id);
        if (!key.isValid())
            continue;
        const auto text = ShortcutManager::keyPressToDisplayString(key);
        for (auto c = text.getCharPointer(); !c.isEmpty(); ++c)
            EXPECT_GE(static_cast<int>(*c), 32) << id << ": '" << text << "'";
        EXPECT_FALSE(text.endsWith(" + ")) << id;
    }
    EXPECT_TRUE(ShortcutsSettingsTab::rowMatchesQuery(
        "return", "Open", ShortcutManager::keyPressToDisplayString(juce::KeyPress(juce::KeyPress::returnKey))));
}
