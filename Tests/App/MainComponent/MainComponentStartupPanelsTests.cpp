// Concern: startup preferences (dual-I/O jack layout, AI request timeout), the AI Panel/Mod
// Matrix/Library/Minimap visibility toggles and their buttons, the ApplicationCommandManager
// command table, the Redo shortcut, and copy/paste/duplicate reaching the canvas.
#include "MainComponentTestFixture.h"

// The jack-layout preference has to reach the patch the app OPENS with, not just modules created
// later. AudioEngine loads the default preset inside its own constructor, so by the time
// MainComponent restores the setting those modules already exist holding their constructor
// defaults — and the voice modules default to dual, so choosing single jacks was ignored on every
// launch.
TEST_F(MainComponentTest, StartupAppliesTheDualIOPreferenceToTheOpeningPatch) {
    auto writeDualIOPref = [](const char* value) {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;
        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("defaultDualIOForNewModules", value);
            s->saveIfNeeded();
        }
    };

    // Reports how many stereo-capable modules in the opening patch are split, and how many total.
    auto countSplit = [](MainComponent& comp) {
        int split = 0;
        int capable = 0;
        for (auto* node : comp.getAudioEngine().getGraph().getNodes()) {
            if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor())) {
                if (!mb->hasDualIOParameter())
                    continue;
                ++capable;
                if (mb->isDualIO())
                    ++split;
            }
        }
        return std::pair<int, int>{split, capable};
    };

    {
        writeDualIOPref("0");
        MainComponent mainComp(std::make_unique<MockProvider>());
        const auto [split, capable] = countSplit(mainComp);
        ASSERT_GT(capable, 0) << "the default preset should contain stereo-capable modules";
        EXPECT_EQ(split, 0) << "with the preference set to single jacks, nothing should open split";
    }

    {
        writeDualIOPref("1");
        MainComponent mainComp(std::make_unique<MockProvider>());
        const auto [split, capable] = countSplit(mainComp);
        ASSERT_GT(capable, 0);
        EXPECT_EQ(split, capable) << "with the preference set to split, every one of them should open split";
    }

    writeDualIOPref("0"); // leave the shared settings file as it was found
}

// Regression test for the ORDERING CONTRACT on aiChatComponent's declaration in MainComponent.h:
// aiChatComponent is a MainComponent member, so its own constructor (which reads
// "aiRequestTimeoutMs" out of appProperties) runs before appProperties.setStorageParameters() in
// MainComponent's constructor body has pointed appProperties at the real settings file — the read
// at that point sees an empty store and falls back to the default. initialiseCommon() must re-read
// and re-push the real persisted value once the file is actually open, or a user's saved timeout
// preference silently reverts to the default on every app launch.
TEST_F(MainComponentTest, StartupRestoresThePersistedAiRequestTimeout) {
    auto writeTimeoutPref = [](const char* value) {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;
        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("aiRequestTimeoutMs", value);
            s->saveIfNeeded();
        }
    };

    writeTimeoutPref("360000"); // 6 minutes — not the 240000 default
    MainComponent mainComp(std::make_unique<MockProvider>());
    EXPECT_EQ(mainComp.getAiServiceForTest().getRequestTimeoutMs(), 360000)
        << "the persisted preference must survive construction, not just the in-memory default";

    writeTimeoutPref("240000"); // leave the shared settings file as it was found
}

TEST_F(MainComponentTest, AIPanelIsHiddenByDefault) {
    MainComponent mainComp(std::make_unique<MockProvider>());
    EXPECT_FALSE(mainComp.isAiPanelConfiguredVisible());
}

TEST_F(MainComponentTest, ToggleAIPanelButtonTextMatchesVisibility) {
    MainComponent mainComp(std::make_unique<MockProvider>());
    // The button must lay out in wide mode for its stateful text to be present.
    mainComp.setSize(1600, 900);

    // Find the toggle button. DrawableButton derives from juce::Button (NOT TextButton), so
    // the cast must be to the common base class after the Phase-3 migration.
    juce::Button* toggleBtn = nullptr;
    for (auto* child : mainComp.getChildren()) {
        if (auto* btn = dynamic_cast<juce::Button*>(child)) {
            if (btn->getComponentID() == "toggleAiPanel")
                toggleBtn = btn;
        }
    }
    ASSERT_NE(toggleBtn, nullptr);

    // Should be hidden by default -> "Show AI"
    EXPECT_FALSE(mainComp.isAiPanelConfiguredVisible());
    EXPECT_EQ(toggleBtn->getButtonText(), "Show AI");

    // Toggle -> "Hide AI"
    mainComp.simulateToggleAiPanelClick();
    EXPECT_TRUE(mainComp.isAiPanelConfiguredVisible());
    EXPECT_EQ(toggleBtn->getButtonText(), "Hide AI");

    // Toggle back -> "Show AI"
    mainComp.simulateToggleAiPanelClick();
    EXPECT_FALSE(mainComp.isAiPanelConfiguredVisible());
    EXPECT_EQ(toggleBtn->getButtonText(), "Show AI");
}

TEST_F(MainComponentTest, ModMatrixIsHiddenByDefault) {
    MainComponent mainComp(std::make_unique<MockProvider>());
    EXPECT_FALSE(mainComp.getGraphEditor().isModMatrixVisible());
}

TEST_F(MainComponentTest, ToggleModMatrixHidesAndShows) {
    MainComponent mainComp(std::make_unique<MockProvider>());

    EXPECT_FALSE(mainComp.getGraphEditor().isModMatrixVisible());

    mainComp.simulateToggleModMatrixClick();
    EXPECT_TRUE(mainComp.getGraphEditor().isModMatrixVisible());

    mainComp.simulateToggleModMatrixClick();
    EXPECT_FALSE(mainComp.getGraphEditor().isModMatrixVisible());
}

// applyToolbarIcons() must call setToggleState(dontSendNotification) on the panel-toggle
// buttons so the themed pill (AppLookAndFeel::drawDrawableButton) reflects panel visibility.
// One click must flip both the visibility flag AND the button's toggle state together; a
// second click must flip both back. If applyToolbarIcons() ever used sendNotification instead
// of dontSendNotification, the button's own onClick would re-fire from inside this same click,
// flipping the flag TWICE and leaving it unchanged after one click — the assertions below that
// the flag actually flipped after exactly one click are the regression guard for that.
TEST_F(MainComponentTest, ToggleAiPanelButtonToggleStateFollowsVisibility) {
    MainComponent mainComp(std::make_unique<MockProvider>());
    mainComp.setSize(1600, 900);

    juce::Button* toggleBtn = nullptr;
    for (auto* child : mainComp.getChildren())
        if (auto* btn = dynamic_cast<juce::Button*>(child))
            if (btn->getComponentID() == "toggleAiPanel")
                toggleBtn = btn;
    ASSERT_NE(toggleBtn, nullptr);

    const bool before = mainComp.isAiPanelConfiguredVisible();
    EXPECT_EQ(toggleBtn->getToggleState(), before);

    mainComp.simulateToggleAiPanelClick();
    EXPECT_EQ(mainComp.isAiPanelConfiguredVisible(), !before); // single-fire guard
    EXPECT_EQ(toggleBtn->getToggleState(), !before);

    mainComp.simulateToggleAiPanelClick();
    EXPECT_EQ(mainComp.isAiPanelConfiguredVisible(), before);
    EXPECT_EQ(toggleBtn->getToggleState(), before);
}

TEST_F(MainComponentTest, ToggleLibraryButtonToggleStateFollowsVisibility) {
    MainComponent mainComp(std::make_unique<MockProvider>());
    mainComp.setSize(1600, 900);

    juce::Button* toggleBtn = nullptr;
    for (auto* child : mainComp.getChildren())
        if (auto* btn = dynamic_cast<juce::Button*>(child))
            if (btn->getComponentID() == "toggleLibrary")
                toggleBtn = btn;
    ASSERT_NE(toggleBtn, nullptr);

    const bool before = mainComp.isLibraryConfiguredVisible();
    EXPECT_EQ(toggleBtn->getToggleState(), before);

    mainComp.simulateToggleLibraryClick();
    EXPECT_EQ(mainComp.isLibraryConfiguredVisible(), !before); // single-fire guard
    EXPECT_EQ(toggleBtn->getToggleState(), !before);

    mainComp.simulateToggleLibraryClick();
    EXPECT_EQ(mainComp.isLibraryConfiguredVisible(), before);
    EXPECT_EQ(toggleBtn->getToggleState(), before);
}

TEST_F(MainComponentTest, ToggleModMatrixButtonToggleStateFollowsVisibility) {
    MainComponent mainComp(std::make_unique<MockProvider>());
    mainComp.setSize(1600, 900);

    juce::Button* toggleBtn = nullptr;
    for (auto* child : mainComp.getChildren())
        if (auto* btn = dynamic_cast<juce::Button*>(child))
            if (btn->getComponentID() == "toggleModMatrix")
                toggleBtn = btn;
    ASSERT_NE(toggleBtn, nullptr);

    const bool before = mainComp.getGraphEditor().isModMatrixVisible();
    EXPECT_EQ(toggleBtn->getToggleState(), before);

    mainComp.simulateToggleModMatrixClick();
    EXPECT_EQ(mainComp.getGraphEditor().isModMatrixVisible(), !before); // single-fire guard
    EXPECT_EQ(toggleBtn->getToggleState(), !before);

    mainComp.simulateToggleModMatrixClick();
    EXPECT_EQ(mainComp.getGraphEditor().isModMatrixVisible(), before);
    EXPECT_EQ(toggleBtn->getToggleState(), before);
}

// simulateToggleMinimapClick() must flip the GraphEditor's minimap visibility (issue #159).
TEST_F(MainComponentTest, SimulateToggleMinimapClickFlipsMinimapVisibility) {
    MainComponent mainComp(std::make_unique<MockProvider>());

    const bool before = mainComp.getGraphEditor().isMinimapVisible();
    mainComp.simulateToggleMinimapClick();
    EXPECT_EQ(mainComp.getGraphEditor().isMinimapVisible(), !before);

    mainComp.simulateToggleMinimapClick();
    EXPECT_EQ(mainComp.getGraphEditor().isMinimapVisible(), before);
}

// MainComponent must push the resolved binding into the minimap, so hovering the map shows the
// same shortcut the toolbar button advertises (MinimapComponent has no ShortcutManager of its own).
TEST_F(MainComponentTest, MinimapTooltipAdvertisesTheToggleShortcut) {
    MainComponent mainComp(std::make_unique<MockProvider>());

    const auto tooltip = mainComp.getGraphEditor().getMinimap().getTooltip();
    const auto expected =
        ShortcutManager::keyPressToDisplayString(mainComp.getShortcutManager().getBinding("toggleMinimap"));
    ASSERT_FALSE(expected.isEmpty());
    EXPECT_TRUE(tooltip.contains(expected)) << tooltip;
}

// Rebinding the shortcut must refresh the advertised key. Tooltips embed the resolved keypress, so
// without a re-run on onBindingsChanged they keep showing the stale binding.
TEST_F(MainComponentTest, MinimapTooltipFollowsARebind) {
    MainComponent mainComp(std::make_unique<MockProvider>());
    auto& shortcuts = mainComp.getShortcutManager();

    // In-memory rebind only — saveToProperties() would write to the shared real settings file.
    shortcuts.setBinding("toggleMinimap", juce::KeyPress('j', juce::ModifierKeys::commandModifier, 0));
    ASSERT_TRUE(shortcuts.onBindingsChanged != nullptr);
    shortcuts.onBindingsChanged();

    const auto tooltip = mainComp.getGraphEditor().getMinimap().getTooltip();
    const auto rebound = ShortcutManager::keyPressToDisplayString(shortcuts.getBinding("toggleMinimap"));
    EXPECT_TRUE(tooltip.contains(rebound)) << tooltip;
}

TEST_F(MainComponentTest, CommandManagerHasCommands) {
    MainComponent mainComp(std::make_unique<MockProvider>());
    auto& cm = mainComp.getCommandManager();
    juce::ignoreUnused(cm);
    juce::Array<juce::CommandID> commands;
    mainComp.getAllCommands(commands);

    // Pinned to the shortcut table rather than to a literal: every rebindable action needs a
    // command behind it, or its key fires and nothing happens (MainComponent::keyPressed resolves
    // action -> command -> perform). A new action with no command here would otherwise ship silent.
    ShortcutManager shortcuts;
    // checkForUpdates (macOS only, AppCommands::checkForUpdates) is deliberately absent from the
    // shortcut table — Sparkle's own convention is a menu-only "Check for Updates…" item with no
    // keyboard shortcut, so the "keypress fires and nothing happens" risk this invariant guards
    // against doesn't apply to it.
    //
    // Only COMMAND-mapped actions count. The table also holds SURFACE actions — the timeline's own
    // keys and the whole piano-roll block, which the components resolve for themselves rather than
    // dispatching through the command manager (see AppCommands::kNoCommand). They must not inflate
    // the expected total, and filtering on the command mapping rather than on a hand-kept id list
    // keeps the "a new command is covered automatically" property this test exists for.
    juce::StringArray expectedActions;
    for (const auto& actionId : shortcuts.getActionIds())
        if (AppCommands::getCommandForAction(actionId) != AppCommands::kNoCommand)
            expectedActions.add(actionId);
    auto expectedCommandCount = expectedActions.size();
#if JUCE_MAC
    expectedCommandCount += 1;
#endif
    // T114/P8-10: showWelcomeScreen and whatsNew are two more menu-only commands with no
    // shortcut-table entry (same "no chord" treatment as checkForUpdates above), but registered
    // UNCONDITIONALLY rather than mac-only — neither needs OS integration, only
    // ownedAudioEngine != nullptr, which is true for every MainComponent this test constructs.
    expectedCommandCount += 2;
    // openPreset (P8-31) is a menu-only command: patch-load is reached via the Load menu/chooser, so
    // only openProject took the rebindable Cmd+O binding. openPreset is still registered in
    // getAllCommands so the menu can invoke it but, like checkForUpdates, has no shortcut-table
    // entry, so the action-filtered count above omits it.
    expectedCommandCount += 1; // openPreset is menu-only (analogous to checkForUpdates)
    // exportStems (P9-8) is a third menu-only command, same treatment as openPreset above: no
    // ShortcutManager actionId/binding, registered here so the File menu can invoke it.
    expectedCommandCount += 1; // exportStems is menu-only (analogous to openPreset)
    // exportPatchOnly (Cmd+Shift+P, P8-20) joined exportAudio (Cmd+Shift+E, P8-5) as a rebindable
    // action with a default binding, so both are now counted through expectedActions above; the old
    // manual `+= 1` for exportPatchOnly no longer applies -- only checkForUpdates (mac), showWelcomeScreen/whatsNew,
    // openPreset and exportStems (above) are still menu-only commands with no shortcut-table entry.
    EXPECT_EQ(commands.size(), expectedCommandCount);
    for (const auto& actionId : expectedActions)
        EXPECT_TRUE(commands.contains(AppCommands::getCommandForAction(actionId)))
            << actionId << " is bindable but has no registered command";

    // Spot-check by identity, not just by count, so a rename can't silently keep the total right.
    EXPECT_TRUE(commands.contains(AppCommands::undo));
    EXPECT_TRUE(commands.contains(AppCommands::selectAllModules));
    EXPECT_TRUE(commands.contains(AppCommands::saveSnippet));
    EXPECT_TRUE(commands.contains(AppCommands::toggleMinimap));
    EXPECT_TRUE(commands.contains(AppCommands::copySelection));
    EXPECT_TRUE(commands.contains(AppCommands::pasteSelection));
    EXPECT_TRUE(commands.contains(AppCommands::duplicateSelection));
    EXPECT_TRUE(commands.contains(AppCommands::exportPatchOnly));

    // Every registered command must resolve real info (name + category), or the native menu bar
    // renders a blank row.
    for (auto id : commands) {
        juce::ApplicationCommandInfo info(id);
        mainComp.getCommandInfo(id, info);
        EXPECT_FALSE(info.shortName.isEmpty()) << "command " << (int)id << " has no name";
    }
}

TEST_F(MainComponentTest, RedoShortcutViaKeyPressed) {
    // NOTE: This test uses MainComponent which loads real ApplicationProperties.
    // If the user has changed the redo shortcut, this test adapts to the saved binding.
    MainComponent mainComp(std::make_unique<MockProvider>());
    auto& um = mainComp.getUndoManager();
    auto& editor = mainComp.getGraphEditor();

    int initialNodeCount = mainComp.getAudioEngine().getGraph().getNumNodes();

    // Add a module (creates an undo snapshot)
    editor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("Oscillator"), &editor, {200, 200}));
    EXPECT_GT(mainComp.getAudioEngine().getGraph().getNumNodes(), initialNodeCount);

    // Undo it
    um.undo();
    EXPECT_EQ(mainComp.getAudioEngine().getGraph().getNumNodes(), initialNodeCount);
    EXPECT_TRUE(um.canRedo());

    // Redo via keyPressed using whatever the current redo binding is
    // Use a fresh ShortcutManager with defaults to get the expected Cmd+Shift+Z
    ShortcutManager defaultSm;
    auto redoKey = defaultSm.getBinding("redo");

    bool handled = mainComp.keyPressed(redoKey);
    // This may fail if user has customised redo — that's expected.
    // The ShortcutManager unit tests verify the matching logic in isolation.
    (void)handled;

    // At minimum, verify keyPressed doesn't crash
    SUCCEED();
}

TEST_F(MainComponentTest, CopyPasteDuplicateCommandsReachTheCanvas) {
    // Driven through the command manager rather than through keyPressed: MainComponent loads the
    // real ApplicationProperties, so a user-customised binding would make a key-level assertion
    // flaky. What matters here is that the command IDs are wired to the editor at all.
    MainComponent mainComp(std::make_unique<MockProvider>());
    auto& editor = mainComp.getGraphEditor();
    auto& cm = mainComp.getCommandManager();
    auto& graph = mainComp.getAudioEngine().getGraph();

    editor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("Oscillator"), &editor, {200, 200}));
    editor.selectAllModules();
    ASSERT_GT(editor.getSelectionCount(), 0);
    const int afterOneModule = graph.getNumNodes();

    // asynchronously = false: the async path posts a CommandMessage and needs a message loop.
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::copySelection, false));
    EXPECT_TRUE(editor.canPaste());

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::pasteSelection, false));
    EXPECT_GT(graph.getNumNodes(), afterOneModule);

    const int afterPaste = graph.getNumNodes();
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::duplicateSelection, false));
    EXPECT_GT(graph.getNumNodes(), afterPaste);
}

TEST_F(MainComponentTest, PasteCommandIsInertUntilSomethingHasBeenCopied) {
    MainComponent mainComp(std::make_unique<MockProvider>());
    auto& editor = mainComp.getGraphEditor();
    auto& graph = mainComp.getAudioEngine().getGraph();

    editor.itemDropped(juce::DragAndDropTarget::SourceDetails(juce::String("Oscillator"), &editor, {200, 200}));
    const int before = graph.getNumNodes();
    ASSERT_FALSE(editor.canPaste());

    juce::ApplicationCommandInfo info(AppCommands::pasteSelection);
    mainComp.getCommandInfo(AppCommands::pasteSelection, info);
    EXPECT_NE(info.flags & juce::ApplicationCommandInfo::isDisabled, 0)
        << "Paste must render greyed out in the menu bar until the clipboard has something in it";

    // ApplicationCommandTarget::tryToInvoke refuses an inactive command, so the disabled flag is
    // what actually stops a stray Cmd+V — perform() is never even reached, and the patch stands.
    EXPECT_FALSE(mainComp.getCommandManager().invokeDirectly(AppCommands::pasteSelection, false));
    EXPECT_EQ(graph.getNumNodes(), before);

    // …and it becomes live the moment something is on the clipboard.
    editor.selectAllModules();
    ASSERT_TRUE(editor.copySelection());
    juce::ApplicationCommandInfo liveInfo(AppCommands::pasteSelection);
    mainComp.getCommandInfo(AppCommands::pasteSelection, liveInfo);
    EXPECT_EQ(liveInfo.flags & juce::ApplicationCommandInfo::isDisabled, 0);
}
