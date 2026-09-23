// Concern: AI provider/model selection on startup (registry-driven default, the
// post-setProvider() refresh regression lock), toolbar button bounds after construction,
// the Locate Master command reached through the real Cmd+Shift+M key path, and (FRO193) that a
// test-constructed MainComponent never touches the real MIDI Remote controller profiles folder.
#include "MainComponentTestFixture.h"
#include "MidiRemote/ControllerProfileStore.h"

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

// FRO45: Cmd+Shift+M drives the same real key-handling path FocusRegionTests.cpp's Cmd+Shift+T/L
// tests use (MainComponent::keyPressed -> ApplicationCommandManager::invokeDirectly, async), not a
// direct call to GraphEditor::locateMasterOrOutput() — pinning that the shortcut is actually wired
// through ShortcutManager/AppCommands, not just that the underlying action works.
TEST_F(MainComponentTest, LocateMasterCmdShiftMSelectsMasterThroughTheRealKeyPath) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1200, 800);

    auto masterNode = mc.getAudioEngine().getGraph().addNode(std::make_unique<MasterModule>());
    masterNode->properties.set("x", 8000);
    masterNode->properties.set("y", 8000);
    mc.getGraphEditor().updateComponents();
    ASSERT_TRUE(mc.getGraphEditor().getSelectedNodes().empty()) << "precondition: nothing selected yet";

    const auto binding = mc.getShortcutManager().getBinding("locateMaster");
    ASSERT_TRUE(binding.isValid());
    EXPECT_TRUE(mc.keyPressed(binding));
    // Async, like every other command MainComponent::keyPressed dispatches (see
    // FocusRegionTests.cpp's Cmd+Shift+T/L tests for the same pattern) — perform() runs on the
    // next message-loop pump, not synchronously inside keyPressed() itself.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    const auto selected = mc.getGraphEditor().getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0], masterNode->nodeID);
}

// FRO193: MidiLearnController's own ControllerProfileStore ctor param used to default to the REAL,
// resolved <settings folder>/MidiRemote/Controllers directory regardless of which MainComponent
// ctor ran it -- so every one of the ~50 MainComponent*Tests.cpp files across this suite (this one
// included, none of which know or care that MIDI Remote exists) silently read, and via a learn
// could have written, the developer's own real controller profile files. Tests/TestMain.cpp now
// calls MainComponent::setControllerProfileTestDirectory() once, before any test runs, pointing
// every MainComponent built through the delegating ctor (the one every test file above uses) at a
// temp directory instead -- this proves that actually took effect, rather than trusting it silently.
TEST_F(MainComponentTest, ConstructedForTestsNeverPointsAtTheRealControllerProfilesFolder) {
    MainComponent mc(std::make_unique<MockProvider>());

    const auto& dir = mc.getMidiLearnControllerForTest().getControllersDirectoryForTest();
    const auto realDir = synth::ControllerProfileStore::resolveDefaultControllersDirectory();

    EXPECT_NE(dir, realDir) << "a MainComponent built for a test must never point at the real settings folder";
    EXPECT_TRUE(dir.getFullPathName().contains("agentsynth-tests-controller-profiles"))
        << "expected Tests/TestMain.cpp's test override directory, got: " << dir.getFullPathName();
}
