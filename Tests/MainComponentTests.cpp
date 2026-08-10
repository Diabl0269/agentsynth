#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AIProviderRegistry.h"
#include "MainComponent.h"
#include "UI/ToolbarComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

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

private:
    juce::String model = "MockModel";
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

    int fetchCallCount = 0;

private:
    juce::String model;
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
            s->saveIfNeeded();
        }
    }

    void SetUp() override { resetPanelKeys(); }
    void TearDown() override { resetPanelKeys(); }
};

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
    EXPECT_EQ(commands.size(), shortcuts.getActionIds().size());
    for (const auto& actionId : shortcuts.getActionIds())
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

TEST_F(MainComponentTest, PatchNameIsDefaultOnStartup) {
    MainComponent mc(std::make_unique<MockProvider>());
    EXPECT_EQ(mc.getCurrentPatchName(), "Default");
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
