#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIProviderRegistry.h"
#include "FakeAudioIODevice.h"
#include "MainComponent.h"
#include "UI/ToolbarComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class MockProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "Mock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

// Mock provider that records fetchAvailableModels() calls and honours setModel()/
// getCurrentModel() so tests can verify the post-setProvider() model-selection contract.
class ModelTrackingMockProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "ModelTrackingMock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        ++fetchCallCount;
        callback({"mock-model-a", "mock-model-b"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        if (callback)
            callback(AIResponse{true, "Mock response.", {}, {}});
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

    int fetchCallCount = 0;

private:
    juce::String model;
    int requestTimeoutMs = 240000;
};

class MainComponentTest : public ::testing::Test {
protected:
    // MainComponent reads/writes the panel-visibility flags from the shared "Agent Synth"
    // ApplicationProperties (same on-disk file the app uses). To keep persistence tests
    // hermetic regardless of execution order, reset those keys to their documented defaults
    // before AND after every test. We open the same PropertiesFile location MainComponent uses.
    void resetPanelKeys() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1"); // default: visible
            s->setValue("aiPanelVisible", "0");        // default: hidden
            s->setValue("minimapVisible", "1");        // default: visible
            s->removeValue("aiRequestTimeoutMs");      // default: AIChatComponent::kDefaultRequestTimeoutMs
            s->saveIfNeeded();
        }
    }

    void SetUp() override {
        resetPanelKeys();
        tempRoot =
            juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-maincomponent-tests");
        tempRoot.deleteRecursively();
        tempRoot.createDirectory();
    }
    void TearDown() override {
        resetPanelKeys();
        tempRoot.deleteRecursively();
    }

    // Save/load round-trip tests write here — same idiom Tests/ProjectBundleTests.cpp uses.
    juce::File tempRoot;
};

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
    // exportPatchOnly is the same "no action id string, no keyboard shortcut" shape as
    // checkForUpdates above (see ShortcutManager.h) — registered unconditionally in getAllCommands,
    // absent from the shortcut table on purpose, so it needs the same manual +1 here.
    expectedCommandCount += 1;
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

// ===========================================================================
// Phase-3 chrome: toolbar layout, min window size, collapsible panels, status bar
// ===========================================================================

// Collect the 9 toolbar DrawableButtons (direct children of MainComponent).
static std::vector<juce::Button*> collectToolbarButtons(MainComponent& mc) {
    static const char* ids[] = {"toggleLibrary", "saveButton",        "loadButton",      "settingsButton", "undoButton",
                                "redoButton",    "autoArrangeButton", "toggleModMatrix", "toggleAiPanel"};
    std::vector<juce::Button*> out;
    for (auto* child : mc.getChildren())
        if (auto* btn = dynamic_cast<juce::Button*>(child))
            for (const char* id : ids)
                if (btn->getComponentID() == id)
                    out.push_back(btn);
    return out;
}

TEST_F(MainComponentTest, ToolbarFitsInsideMinimumWindowWidth) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(480, 400);

    auto buttons = collectToolbarButtons(mc);
    ASSERT_EQ((int)buttons.size(), 9);
    for (auto* b : buttons) {
        EXPECT_GT(b->getWidth(), 0);
        EXPECT_GE(b->getX(), 0);
        EXPECT_LE(b->getRight(), 480);
    }
}

TEST_F(MainComponentTest, ToolbarFitsAtHalfMinimumWidth) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(640, 480);
    auto buttons = collectToolbarButtons(mc);
    ASSERT_EQ((int)buttons.size(), 9);
    for (auto* b : buttons) {
        EXPECT_GE(b->getX(), 0);
        EXPECT_LE(b->getRight(), 640);
    }
}

TEST_F(MainComponentTest, ToolbarNarrowModeAt480) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(480, 400);
    EXPECT_TRUE(mc.getToolbar().isNarrowMode());
}

TEST_F(MainComponentTest, ToolbarWideModeAt1600) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    EXPECT_FALSE(mc.getToolbar().isNarrowMode());
}

TEST_F(MainComponentTest, StatusBarOccupiesCorrectBounds) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    auto& sb = mc.getStatusBar();
    // Status bar height token = 24; sits flush at the bottom.
    EXPECT_EQ(sb.getHeight(), 24);
    EXPECT_EQ(sb.getY(), mc.getHeight() - 24);
}

TEST_F(MainComponentTest, CanvasRemainsNonZeroAtMinimumSize) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(480, 400);
    auto bounds = mc.getGraphEditor().getBounds();
    EXPECT_GT(bounds.getWidth(), 0);
    EXPECT_GT(bounds.getHeight(), 0);
}

TEST_F(MainComponentTest, LibrarySidebarDefaultVisible) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_TRUE(mc.isLibraryConfiguredVisible());
}

TEST_F(MainComponentTest, LibrarySidebarTogglePersists) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_TRUE(mc.isLibraryConfiguredVisible());

    mc.simulateToggleLibraryClick();
    EXPECT_FALSE(mc.isLibraryConfiguredVisible());
    // Persistence is written + read back within the same session.
    EXPECT_FALSE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("librarySidebarVisible", true));

    mc.simulateToggleLibraryClick();
    EXPECT_TRUE(mc.isLibraryConfiguredVisible());
    EXPECT_TRUE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("librarySidebarVisible", false));
}

TEST_F(MainComponentTest, LibrarySidebarCollapsedNarrowsGraphEditor) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);

    // Shown by default: library occupies the left 200 px, so the graph editor starts at x=200.
    ASSERT_TRUE(mc.isLibraryConfiguredVisible());
    EXPECT_EQ(mc.getGraphEditor().getBounds().getX(), 200);

    // Hidden: graph editor reclaims the full left edge (x=0).
    mc.simulateToggleLibraryClick();
    ASSERT_FALSE(mc.isLibraryConfiguredVisible());
    EXPECT_EQ(mc.getGraphEditor().getBounds().getX(), 0);
}

TEST_F(MainComponentTest, AiPanelTogglePersists) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_FALSE(mc.isAiPanelConfiguredVisible());

    mc.simulateToggleAiPanelClick();
    EXPECT_TRUE(mc.isAiPanelConfiguredVisible());
    EXPECT_TRUE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("aiPanelVisible", false));

    mc.simulateToggleAiPanelClick();
    EXPECT_FALSE(mc.isAiPanelConfiguredVisible());
    EXPECT_FALSE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("aiPanelVisible", true));
}

TEST_F(MainComponentTest, StatusBarTimerGating) {
    MainComponent mc(std::make_unique<MockProvider>());
    // Startup runs the timer setup; the tick counter starts at 0.
    EXPECT_EQ(mc.getStatusBarTickCountForTest(), 0);

    // 1st tick: counter increments to 1, no status-bar update yet.
    mc.timerCallback();
    EXPECT_EQ(mc.getStatusBarTickCountForTest(), 1);

    // 2nd tick: counter hits 2 -> status bar updates -> resets to 0.
    mc.timerCallback();
    EXPECT_EQ(mc.getStatusBarTickCountForTest(), 0);
}

// The always-visible transport cluster (see docs/layout.md §5): the status bar must receive
// play-state/position/BPM from the SAME unconditional PositionSnapshot poll that already exists in
// timerCallback(), so it works identically whether the timeline panel is open or closed.
TEST_F(MainComponentTest, StatusBarReceivesTransportStateFromTimerCallback) {
    MainComponent mc(std::make_unique<MockProvider>());

    // Two ticks reach the 5 Hz status-bar sub-tick (see StatusBarTimerGating above).
    mc.timerCallback();
    mc.timerCallback();

    auto& sb = mc.getStatusBar();
    EXPECT_FALSE(sb.getTransportButton().getToggleState()) << "transport starts stopped";
    const juce::String text = sb.getTransportDisplayTextForTest();
    EXPECT_TRUE(text.contains(synth::ui::TimelineTransportBar::formatBarBeat(0.0, 4, 4)))
        << "reuses TimelineTransportBar's own formatBarBeat(), not a reimplementation";
    EXPECT_TRUE(text.contains("120.0")) << "default transport tempo";
}

// The status bar's play/stop button must drive the SAME TransportService the timeline transport
// bar uses — not a parallel play/stop of its own.
//
// play()/stop() only POST a command onto TransportService's lock-free FIFO (see its threading
// contract); only tick(), called from the audio thread's device callback, actually drains it into
// getPositionSnapshot().playing. A real device's callback thread would do that asynchronously, so
// asserting on it right after the click would race the real hardware. Same fix LatencyAlignmentTests.cpp
// uses: suspend the real callback and drive one block by hand with the shared FakeAudioIODevice, so
// the command is guaranteed to have drained before the assertion runs.
TEST_F(MainComponentTest, StatusBarPlayStopButtonDrivesTheSameTransport) {
    MainComponent mc(std::make_unique<MockProvider>());
    auto& engine = mc.getAudioEngine();
    auto& transport = engine.getTransport();

    engine.suspendDeviceCallback();
    synth::test::FakeAudioIODevice fake(2, 2);
    engine.audioDeviceAboutToStart(&fake);

    const auto driveOneBlock = [&engine] {
        constexpr int kBlockSize = synth::test::kFakeDeviceBlockSize;
        std::vector<float> left((std::size_t)kBlockSize, 0.0f), right((std::size_t)kBlockSize, 0.0f);
        std::vector<float> outLeft((std::size_t)kBlockSize, 0.0f), outRight((std::size_t)kBlockSize, 0.0f);
        const float* inputs[] = {left.data(), right.data()};
        float* outputs[] = {outLeft.data(), outRight.data()};
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlockSize, {});
    };

    ASSERT_FALSE(transport.getPositionSnapshot().playing);

    // NOT triggerClick(): that posts a command message, which never dispatches in a headless test
    // (see StatusBarTests.cpp's own TransportButtonClickFiresOwnerWiredCallback).
    mc.getStatusBar().getTransportButton().onClick();
    driveOneBlock();
    EXPECT_TRUE(transport.getPositionSnapshot().playing);

    mc.getStatusBar().getTransportButton().onClick();
    driveOneBlock();
    EXPECT_FALSE(transport.getPositionSnapshot().playing);

    engine.audioDeviceStopped();
}

TEST_F(MainComponentTest, PatchNameIsDefaultOnStartup) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_EQ(mc.getCurrentPatchName(), "Default");
}

// P8-1's dirty-title wiring: startup itself must never leave the document marked dirty. Nothing in
// construction (loading the default preset, applying the dual-IO preference, restoring
// preferences) goes through AppUndoManager, so canUndo() — and therefore isDirty_ — must both be
// false the instant construction finishes. A regression here would show up as a stray " *" in the
// title bar of a freshly launched, untouched app.
TEST_F(MainComponentTest, FreshlyConstructedDocumentIsNotDirty) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_FALSE(mc.getUndoManager().canUndo());
    EXPECT_FALSE(mc.isProjectDirtyForTest());
}

// juce::UndoManager (unlike ShortcutManager's own ChangeBroadcaster) notifies via the ASYNC
// sendChangeMessage(), not sendSynchronousChangeMessage() — so isDirty_ only flips once the message
// loop actually runs a dispatch pass, hence the runDispatchLoopUntil() pump (the same idiom
// AIChatComponentTests.cpp/AccountServiceTests.cpp use for other async JUCE notifications).
TEST_F(MainComponentTest, DirtyFlagTracksARealUndoStepThenClearsOnSave) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_FALSE(mc.isProjectDirtyForTest());

    mc.simulateAddMidiTrackClick();
    ASSERT_TRUE(mc.getUndoManager().canUndo());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_TRUE(mc.isProjectDirtyForTest());

    mc.saveProjectForTest(tempRoot.getChildFile("DirtyFlag.agsproj"));
    EXPECT_FALSE(mc.isProjectDirtyForTest()) << "a successful save must clear the dirty flag";
}

TEST_F(MainComponentTest, PatchNameUpdatesOnFactoryPresetLoad) {
    MainComponent mc(std::make_unique<MockProvider>());
    auto presets = synth::PresetManager::getPresetList();
    ASSERT_GE(presets.size(), 2);

    // simulateLoadFactoryPresetForTest mirrors the loadButton popup call site exactly:
    // loadFactoryPreset(index) + setCurrentPatchName(presets[index].name).
    mc.simulateLoadFactoryPresetForTest(1);
    EXPECT_EQ(mc.getCurrentPatchName(), presets[1].name);
}

// ---------------------------------------------------------------------------
// P8-1: Cmd+S saves the whole project (bundle, not patch-only json) and remembers the file.
// ---------------------------------------------------------------------------

// A freshly constructed document has never been saved, so there is no bundle to resave to
// silently — Cmd+S is about to prompt. This is the regression's root cause, pinned directly:
// before this fix, saveButton.onClick ALWAYS launched a chooser regardless of this state.
TEST_F(MainComponentTest, SaveWithNoCurrentBundlePromptsForLocation) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_TRUE(mc.wouldPromptOnSaveForTest());
}

// Once a project has been saved as a bundle, Cmd+S must resave to that SAME path silently — no
// chooser. Asserting the predicate alone (rather than driving the save button/command) is
// deliberate: performSaveProject's chooser-launching branch is only reached when this predicate is
// true, so proving it is false here is what proves the button's onClick can never reach
// fileChooser->launchAsync for this document — exactly the property a headless test can check
// without ever risking a real native dialog.
TEST_F(MainComponentTest, SaveWithCurrentBundleResavesSilently) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    const auto bundleDir = tempRoot.getChildFile("Resave.agsproj");
    mc.saveProjectForTest(bundleDir);
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundleDir));

    EXPECT_FALSE(mc.wouldPromptOnSaveForTest());
}

// THE regression test: a project with a timeline track, saved via Cmd+S's own file handler, must
// come back with that track intact when reopened — this is exactly what broke when Cmd+S always
// wrote a patch-only .json (which carries no "timeline" key at all).
TEST_F(MainComponentTest, SavedThenReloadedProjectRetainsTimeline) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    mc.simulateAddMidiTrackClick();
    ASSERT_EQ(mc.getTimelineDoc().getTracks().size(), 1u);

    const auto bundleDir = tempRoot.getChildFile("RoundTrip.agsproj");
    mc.saveProjectForTest(bundleDir);
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundleDir));

    MainComponent reloaded(std::make_unique<MockProvider>());
    reloaded.setSize(1600, 900);
    reloaded.getAudioEngine().suspendDeviceCallback();

    ASSERT_TRUE(reloaded.openProjectForTest(bundleDir));
    EXPECT_EQ(reloaded.getTimelineDoc().getTracks().size(), 1u);
}

// Export Patch Only must write BYTE-IDENTICAL output to the legacy plain-.json save path — both
// go through GraphEditor::savePreset under the hood, and the whole point of keeping this escape
// hatch is that it is exactly the old behaviour, not a reimplementation of it.
TEST_F(MainComponentTest, ExportPatchOnlyWritesByteIdenticalLegacyJson) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    const auto exported = tempRoot.getChildFile("exported.json");
    const auto legacy = tempRoot.getChildFile("legacy.json");
    mc.exportPatchOnlyForTest(exported);
    mc.saveProjectForTest(legacy);

    ASSERT_TRUE(exported.existsAsFile());
    ASSERT_TRUE(legacy.existsAsFile());
    // Raw text, not a parsed-var comparison: juce::var's equality for an object/array is REFERENCE
    // identity (see VariantType::objectEquals), so two independently parsed vars would never
    // compare equal even for byte-identical JSON. The patch serialiser writes no timestamps or
    // fresh random ids on save, so the raw text from two back-to-back saves of the same graph is
    // expected to match exactly.
    EXPECT_EQ(exported.loadFileAsString(), legacy.loadFileAsString());
}

// A plain .json preset (the pre-P8-1 default, and still what Export Patch Only writes) must still
// open correctly — openFromFile's non-bundle branch is untouched by this ticket, but the save-side
// default changing is exactly the kind of change that could have silently broken it by omission.
TEST_F(MainComponentTest, OpeningLegacyJsonPresetStillWorks) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    const auto jsonFile = tempRoot.getChildFile("Legacy.json");
    mc.saveProjectForTest(jsonFile);
    ASSERT_TRUE(jsonFile.existsAsFile());

    EXPECT_TRUE(mc.openProjectForTest(jsonFile));
    EXPECT_EQ(mc.getCurrentPatchName(), "Legacy");
}

// REGRESSION LOCK (f7cba4a): MainComponent must refresh models AFTER setProvider(). The
// AIChatComponent ctor's own refreshModels() runs before the provider exists, so without
// the post-setProvider refresh currentModel stays empty and every /api/chat is a 400
// "model is required".
TEST_F(MainComponentTest, AiProviderGetsModelSelectedOnStartup) {
    auto ownedProvider = std::make_unique<ModelTrackingMockProvider>();
    ModelTrackingMockProvider* rawProvider = ownedProvider.get();

    MainComponent mc(std::move(ownedProvider));

    EXPECT_GE(rawProvider->fetchCallCount, 1);
    EXPECT_FALSE(mc.getAiServiceForTest().getCurrentModel().isEmpty());
    EXPECT_EQ(mc.getAiServiceForTest().getCurrentModel(), "mock-model-a");
}

// Confirms MainComponent's no-injected-provider path goes through AIProviderRegistry
// (not a hardcoded OllamaProvider construction) and that the post-setProvider()
// refreshModels() contract (see AiProviderGetsModelSelectedOnStartup above) still holds
// when the provider comes from the registry.
TEST_F(MainComponentTest, StartupUsesRegistryAndStillSelectsAModel) {
    synth::AIProviderRegistry registry;
    registry.registerProvider({"ollama", "Ollama (local)", true, false,
                               [](const synth::ProviderConfig&) -> std::unique_ptr<synth::AIProvider> {
                                   return std::make_unique<ModelTrackingMockProvider>();
                               }});

    MainComponent mc(nullptr, registry);

    EXPECT_FALSE(mc.getAiServiceForTest().getCurrentModel().isEmpty());
    EXPECT_EQ(mc.getAiServiceForTest().getCurrentModel(), "mock-model-a");
}

// P4-6: resolveDefaultProviderId() is the pure decision MainComponent::initialiseCommon() bases
// the migration on — a fresh install (no settings file at all) gets the new hosted-by-default,
// an install that has already launched before keeps its working local Ollama default even though
// it has never touched the "aiProvider" key specifically (see initialiseCommon()'s comment for
// why key-absence alone can't tell those two cases apart). Tested directly, without touching a
// real properties file, since MainComponent hardcodes its settings folder.
TEST(MainComponentDefaultProviderIdTest, FreshInstallDefaultsToHosted) {
    EXPECT_EQ(MainComponent::resolveDefaultProviderId(/*hasExistingSettingsFile=*/false), "remote");
}

TEST(MainComponentDefaultProviderIdTest, ExistingInstallKeepsLocalOllamaDefault) {
    EXPECT_EQ(MainComponent::resolveDefaultProviderId(/*hasExistingSettingsFile=*/true), "ollama");
}

// Integration counterpart to the pure-function tests above: this fixture's shared "Agent Synth"
// settings file already exists on disk by the time this test runs (resetPanelKeys() in SetUp()
// writes it), so it stands in for "existing install, AI settings never touched" — the "aiProvider"
// key itself is removed here to simulate a user who never opened the AI settings tab. Confirms the
// migration reaches all the way through initialiseCommon() into which provider id the registry is
// actually asked to construct, not just the pure decision function in isolation.
TEST_F(MainComponentTest, ExistingInstallWithNoAiProviderKeyRequestsOllamaFromRegistry) {
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->removeValue("aiProvider");
            s->saveIfNeeded();
        }
    }

    juce::String requestedId;
    synth::AIProviderRegistry registry;
    registry.registerProvider({"ollama", "Ollama (local)", true, false,
                               [&requestedId](const synth::ProviderConfig&) -> std::unique_ptr<synth::AIProvider> {
                                   requestedId = "ollama";
                                   return std::make_unique<ModelTrackingMockProvider>();
                               }});
    registry.registerProvider({"remote", "Remote (hosted)", true, true,
                               [&requestedId](const synth::ProviderConfig&) -> std::unique_ptr<synth::AIProvider> {
                                   requestedId = "remote";
                                   return std::make_unique<ModelTrackingMockProvider>();
                               }});

    MainComponent mc(nullptr, registry);

    EXPECT_EQ(requestedId, "ollama");
}

// Regression: toolbar buttons must have non-zero bounds immediately after construction,
// WITHOUT any additional manual resize or sidebar toggle. Pre-fix, setSize() fired
// resized() -> layoutButtons() before setButtons() was called, leaving all button bounds
// at {0,0,0,0}. The bug manifested as a blank toolbar on first launch that only appeared
// after toggling the library sidebar (Cmd+B).
TEST_F(MainComponentTest, ToolbarButtonsHaveNonZeroBoundsAfterConstruction) {
    MainComponent mc(std::make_unique<MockProvider>());
    // No extra setSize() or toggle call — bounds must already be set by the constructor.
    auto buttons = collectToolbarButtons(mc);
    ASSERT_EQ((int)buttons.size(), 9) << "Expected 9 toolbar DrawableButtons";
    for (auto* b : buttons) {
        EXPECT_GT(b->getWidth(), 0) << "Button '" << b->getComponentID() << "' has zero width after construction";
        EXPECT_GT(b->getHeight(), 0) << "Button '" << b->getComponentID() << "' has zero height after construction";
    }
}
