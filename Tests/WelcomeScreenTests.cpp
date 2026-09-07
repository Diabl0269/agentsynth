// T114/P8-10: the app-only welcome screen overlay (Feature 1) and the build-time "What's New"
// dialog (Feature 2). See docs/architecture.md's "Welcome screen" subsection (§5) and
// docs/distribution.md's "What's New" section.
//
// SAFETY RULE, same as MainComponentTests.cpp's unsaved-changes section: never let a real dialog
// open. Every action reachable from the welcome screen ultimately funnels through
// guardUnsavedChanges/openFromFile's own dialogs (unsavedChangesPrompt / a real FileChooser), so:
//   * Install mc.unsavedChangesPrompt BEFORE anything can reach the guard on a DIRTY document.
//   * Never click getOpenExistingButtonForTest() with an answer that would continue into
//     launchOpenProjectChooser() — that opens a real native FileChooser.
//   * showWhatsNewDialog()/AppCommands::whatsNew are never invoked from a test — they open a real
//     modal juce::AlertWindow.

#include "../Source/AI/AIProvider.h"
#include "../Source/AudioEngine.h"
#include "../Source/MainComponent.h"
#include "../Source/ProjectBundle.h"
#include "../Source/UI/Theme/AppLookAndFeel.h"
#include "../Source/UI/Theme/ThemeManager.h"
#include "../Source/UI/WelcomeScreenComponent.h"
#include "WhatsNewData.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace {

// Never touches the network — same role MainComponentTests.cpp's MockProvider and
// PluginProcessorTests.cpp's NullAIProvider play, just one local copy per test TU (see this
// codebase's existing convention of a small per-file mock rather than a shared test-only library).
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

/** Stands in for the unsaved-changes dialog — same idiom as MainComponentTests.cpp's own
 *  PromptRecorder (duplicated here rather than shared across .cpp files, which is how every other
 *  test file in this repo handles a small test-only helper). Leaving `answer` empty models "the
 *  dialog is still up", which is what lets a test assert the document/welcome-screen state WHILE
 *  the question is outstanding. */
struct PromptRecorder {
    int calls = 0;
    juce::String lastLabel;
    std::optional<MainComponent::UnsavedChangesChoice> answer;

    void installOn(MainComponent& mc) {
        mc.unsavedChangesPrompt = [this](const juce::String& label,
                                         std::function<void(MainComponent::UnsavedChangesChoice)> onChoice) {
            ++calls;
            lastLabel = label;
            if (answer.has_value())
                onChoice(*answer);
        };
    }
};

void pumpMessageLoop() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

/** One edit that dirties the document, with the pump already folded in — same as
 *  MainComponentTests.cpp's makeDirty(). */
void makeDirty(MainComponent& mc) {
    mc.simulateAddMidiTrackClick();
    pumpMessageLoop();
}

/** Opens the SAME real "Agent Synth" settings file MainComponent itself uses (synth::
 *  userSettingsOptions()) and writes the "showWelcomeScreenAtLaunch" key directly — same idiom as
 *  MainComponentTests.cpp's per-test writeXxxPref() lambdas. The key is deliberately NOT declared
 *  in Source/UserSettings.h (single-owner, MainComponent only — see its header comment), so this
 *  test file uses the same literal string MainComponent.cpp does. */
void writeShowWelcomeAtLaunchPref(const char* value) {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth";
    opts.folderName = "Agent Synth";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    juce::ApplicationProperties props;
    props.setStorageParameters(opts);
    if (auto* s = props.getUserSettings()) {
        s->setValue("showWelcomeScreenAtLaunch", value);
        s->saveIfNeeded();
    }
}

} // namespace

class WelcomeScreenTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Every test in this file shares the real settings file with every other test process-wide
        // (see MainComponentTests.cpp's resetPanelKeys() for the same concern) — force the default
        // (shown) before AND after each test so run order can't leak a false-hidden welcome screen
        // into an unrelated test.
        writeShowWelcomeAtLaunchPref("1");
        tempRoot =
            juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-welcome-screen-tests");
        tempRoot.deleteRecursively();
        tempRoot.createDirectory();
    }
    void TearDown() override {
        writeShowWelcomeAtLaunchPref("1");
        tempRoot.deleteRecursively();
    }

    juce::File tempRoot;
};

// ---------------------------------------------------------------------------
// Hosted-mode gating — the plugin path must never see this overlay at all.
// ---------------------------------------------------------------------------

// Mirrors PluginProcessorTests.cpp's ExternalEngineSurvivesMainComponentDestruction setup: the
// plugin ctor (external, Hosted-mode engine) is the ONLY one where ownedAudioEngine stays null.
TEST_F(WelcomeScreenTest, NeverConstructsInHostedMode) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;

    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();

    MainComponent mc(tm, lf, engine, std::make_unique<MockProvider>());
    EXPECT_EQ(mc.getWelcomeScreenForTest(), nullptr);

    engine.shutdown();
}

TEST_F(WelcomeScreenTest, ExistsOnTheStandaloneAppPath) {
    MainComponent mc(std::make_unique<MockProvider>());
    ASSERT_NE(mc.getWelcomeScreenForTest(), nullptr);
    EXPECT_TRUE(mc.getWelcomeScreenForTest()->isVisible()) << "shown by default (showWelcomeScreenAtLaunch=true)";
}

// ---------------------------------------------------------------------------
// The persisted "show at launch" preference.
// ---------------------------------------------------------------------------

TEST_F(WelcomeScreenTest, HiddenByDefault_WhenShowAtLaunchPreferenceIsFalse) {
    writeShowWelcomeAtLaunchPref("0");

    MainComponent mc(std::make_unique<MockProvider>());
    ASSERT_NE(mc.getWelcomeScreenForTest(), nullptr);
    EXPECT_FALSE(mc.getWelcomeScreenForTest()->isVisible());
    EXPECT_FALSE(mc.getWelcomeScreenForTest()->getShowAtLaunchToggleForTest().getToggleState());
}

TEST_F(WelcomeScreenTest, ShowAtLaunchToggle_PersistsSetting) {
    MainComponent mc(std::make_unique<MockProvider>());
    ASSERT_NE(mc.getWelcomeScreenForTest(), nullptr);
    auto& toggle = mc.getWelcomeScreenForTest()->getShowAtLaunchToggleForTest();
    ASSERT_TRUE(toggle.getToggleState()) << "default preference is true";

    // juce::Button::triggerClick() posts an async command message (postCommandMessage) rather
    // than flipping state inline — same reasoning as every dirty-flag assertion elsewhere in this
    // codebase needing a pump before it can be observed.
    toggle.triggerClick();
    pumpMessageLoop();

    EXPECT_FALSE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("showWelcomeScreenAtLaunch", true));

    toggle.triggerClick(); // flip back
    pumpMessageLoop();
    EXPECT_TRUE(mc.getAppPropertiesForTest().getUserSettings()->getBoolValue("showWelcomeScreenAtLaunch", false));
}

// ---------------------------------------------------------------------------
// The four actions.
// ---------------------------------------------------------------------------

TEST_F(WelcomeScreenTest, NewProjectButton_ClearsCanvasAndHidesWelcomeScreen) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_NE(mc.getWelcomeScreenForTest(), nullptr);
    ASSERT_TRUE(mc.getWelcomeScreenForTest()->isVisible());

    mc.getWelcomeScreenForTest()->getNewProjectButtonForTest().triggerClick();
    pumpMessageLoop();

    EXPECT_EQ(mc.getCurrentPatchName(), "Untitled");
    EXPECT_FALSE(mc.getWelcomeScreenForTest()->isVisible());
}

TEST_F(WelcomeScreenTest, OpenDefaultButton_LoadsFactoryPresetZeroAndHidesWelcomeScreen) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_NE(mc.getWelcomeScreenForTest(), nullptr);
    ASSERT_TRUE(mc.getWelcomeScreenForTest()->isVisible());

    mc.getWelcomeScreenForTest()->getOpenDefaultButtonForTest().triggerClick();
    pumpMessageLoop();

    EXPECT_EQ(mc.getCurrentPatchName(), "Default"); // PresetManager::getPresetList()[0].name
    EXPECT_FALSE(mc.getWelcomeScreenForTest()->isVisible());
}

// Never lets a real FileChooser open (see the file-header safety rule): with no answer supplied,
// the guard's dialog stays "open" and launchOpenProjectChooser() is never reached.
TEST_F(WelcomeScreenTest, OpenExistingButton_InvokesTheSameGuardedPathAsOpenProject) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_NE(mc.getWelcomeScreenForTest(), nullptr);

    PromptRecorder prompt; // no answer: the dialog stays "open"
    prompt.installOn(mc);
    makeDirty(mc);

    mc.getWelcomeScreenForTest()->getOpenExistingButtonForTest().triggerClick();
    pumpMessageLoop();

    EXPECT_EQ(prompt.calls, 1);
    EXPECT_EQ(prompt.lastLabel, "Opening a project") << "the same label openProjectFromFile() uses";
    EXPECT_TRUE(mc.getWelcomeScreenForTest()->isVisible()) << "no dialog has resolved yet";
}

TEST_F(WelcomeScreenTest, RecentProjectRow_OpensThatFileAndHidesWelcomeScreen) {
    // A separate instance writes the bundle a fresh `mc` will then open via its welcome screen —
    // proves a real state change rather than "still says what it always said".
    MainComponent writer(std::make_unique<MockProvider>());
    writer.setSize(1600, 900);
    writer.getAudioEngine().suspendDeviceCallback();
    const auto bundleDir = tempRoot.getChildFile("WelcomeRecent.agsproj");
    ASSERT_TRUE(writer.saveProjectForTest(bundleDir));
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundleDir));

    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_NE(mc.getWelcomeScreenForTest(), nullptr);
    ASSERT_TRUE(mc.getWelcomeScreenForTest()->isVisible());
    ASSERT_NE(mc.getCurrentPatchName(), bundleDir.getFileNameWithoutExtension());

    mc.getWelcomeScreenForTest()->setRecentProjects({bundleDir});
    ASSERT_EQ(mc.getWelcomeScreenForTest()->getRecentProjectCountForTest(), 1);

    mc.getWelcomeScreenForTest()->triggerRecentProjectForTest(0);
    pumpMessageLoop();

    EXPECT_EQ(mc.getCurrentPatchName(), bundleDir.getFileNameWithoutExtension());
    EXPECT_FALSE(mc.wouldPromptOnSaveForTest()) << "the recent project is now the open bundle";
    EXPECT_FALSE(mc.getWelcomeScreenForTest()->isVisible());
}

// THE headline test: the guard must run BEFORE the welcome screen is ever hidden. hideWelcomeScreen()
// lives inside loadPresetGuarded()'s `proceed` continuation — a Cancel answer must never reach it.
TEST_F(WelcomeScreenTest, DirtyDocumentIsGuardedBeforeWelcomeScreenReplacesIt) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_NE(mc.getWelcomeScreenForTest(), nullptr);
    ASSERT_TRUE(mc.getWelcomeScreenForTest()->isVisible());

    PromptRecorder prompt;
    prompt.answer = MainComponent::UnsavedChangesChoice::Cancel;
    prompt.installOn(mc);
    makeDirty(mc);
    ASSERT_TRUE(mc.isProjectDirty());

    mc.getWelcomeScreenForTest()->getOpenDefaultButtonForTest().triggerClick();
    pumpMessageLoop();

    EXPECT_EQ(prompt.calls, 1);
    EXPECT_EQ(prompt.lastLabel, "Loading a preset");
    EXPECT_EQ(mc.getTimelineDoc().getTracks().size(), 1u) << "Cancel means the load never happened";
    EXPECT_TRUE(mc.isProjectDirty());
    EXPECT_TRUE(mc.getWelcomeScreenForTest()->isVisible()) << "Cancel must not hide the welcome screen either";
}

// ---------------------------------------------------------------------------
// Feature 2: build-time "What's New" (no network, sourced from git history at CMake configure
// time — see the root CMakeLists.txt and WhatsNewData.h). The generated content is
// machine-dependent (whatever this checkout's git history happens to be), so these assert the
// MECHANISM only, never specific commit text.
// ---------------------------------------------------------------------------

TEST(WhatsNewTest, KReleaseTagIsNonEmpty) { EXPECT_FALSE(juce::String(synth::whatsnew::kReleaseTag).isEmpty()); }

TEST(WhatsNewTest, KHighlightsCountMatchesArraySize) {
    ASSERT_GT(synth::whatsnew::kHighlightsCount, 0);
    int totalLength = 0;
    for (int i = 0; i < synth::whatsnew::kHighlightsCount; ++i) {
        const char* entry = synth::whatsnew::kHighlights[i];
        ASSERT_NE(entry, nullptr);
        // Copy-init (`=`, not `Type(entry)` as a bare statement) — the latter is the classic "most
        // vexing parse" shape, and a qualified-name variant of it is what actually failed to
        // compile here on the first pass.
        const juce::String asString = entry;
        totalLength += asString.length();
    }
    // Every real commit subject is non-empty; the "No release history available."/empty-tag
    // fallbacks each still produce exactly one non-empty entry — so this can never be 0 unless a
    // future generator change starts emitting garbage.
    EXPECT_GT(totalLength, 0);
}

// Never calls perform() for this command — it would open a real modal juce::AlertWindow (see the
// file-header safety rule).
TEST_F(WelcomeScreenTest, WhatsNewCommand_IsRegisteredAndActive) {
    MainComponent mc(std::make_unique<MockProvider>());

    juce::Array<juce::CommandID> commands;
    mc.getAllCommands(commands);
    EXPECT_TRUE(commands.contains((juce::CommandID)AppCommands::whatsNew));

    juce::ApplicationCommandInfo info(AppCommands::whatsNew);
    mc.getCommandInfo(AppCommands::whatsNew, info);
    EXPECT_TRUE((info.flags & juce::ApplicationCommandInfo::isDisabled) == 0) << "must not be greyed out";
}

TEST_F(WelcomeScreenTest, ShowWelcomeScreenCommand_IsRegisteredAndActiveOnTheAppPath) {
    MainComponent mc(std::make_unique<MockProvider>());

    juce::Array<juce::CommandID> commands;
    mc.getAllCommands(commands);
    EXPECT_TRUE(commands.contains((juce::CommandID)AppCommands::showWelcomeScreen));

    juce::ApplicationCommandInfo info(AppCommands::showWelcomeScreen);
    mc.getCommandInfo(AppCommands::showWelcomeScreen, info);
    EXPECT_TRUE((info.flags & juce::ApplicationCommandInfo::isDisabled) == 0);
}
