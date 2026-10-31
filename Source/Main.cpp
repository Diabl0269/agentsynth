#include "AutomationLaunch.h"
#include "Branding.h"
#include "MainComponent/MainComponent.h"
#include "MainComponent/WindowKeyForward.h"
#include "Plugin/Hosting/PluginScanService.h"
#include "Project/ProjectCommandLine.h"
#include "SettingsMigration.h"
#include "ShortcutManager/AppCommands.h"
#include "ShortcutManager/ShortcutManager.h"
#include "TestMode/AutomationHiding.h"
#include "TestMode/TestModeLaunch.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include "UserSettings.h"
#include <JuceHeader.h>
#include <iostream>
#include <utility>

// True on every platform where JUCE's entry point is a plain main(argc, argv) — i.e. everything
// except a Windows GUI-subsystem build, whose WinMain gets no argv. See the entry point at the
// bottom of this file for what the difference costs.
#if JUCE_WINDOWS && !defined(_CONSOLE)
#define SYNTH_HAS_ARGV_MAIN 0
#else
#define SYNTH_HAS_ARGV_MAIN 1
#endif

namespace {

/** Writes the child scan's document where the parent's pipe reader will find it. Deliberately
 *  std::cout and not juce::Logger: the parent parses this, and the Logger is hijacked by the AI
 *  console in Debug builds. */
void emitPluginScanChildOutput(const juce::String& xml) {
    if (xml.isNotEmpty())
        std::cout << xml << std::endl;
    std::cout.flush();
}

} // namespace

class AppApplication : public juce::JUCEApplication {
public:
    AppApplication() {}

    const juce::String getApplicationName() override { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
#if !SYNTH_HAS_ARGV_MAIN
        // Windows GUI fallback for the out-of-process plugin scan. Everywhere else main()
        // below short-circuits before the app object is ever constructed; here JUCE owns WinMain and
        // this is the earliest hook there is, so the child pays for an app spin-up. It still creates
        // no window, no engine and no settings file — this runs before all of that.
        {
            juce::String scanXml;
            if (const auto exitCode = synth::runPluginScanChildMode(getCommandLineParameterArray(), scanXml,
                                                                    /*suppressCrashDialog=*/true)) {
                emitPluginScanChildOutput(scanXml);
                setApplicationReturnValue(*exitCode);
                quit();
                return;
            }
        }
#endif

        // Automation launches (--no-audio-device / --no-keychain) must never read the keychain, so
        // this runs before anything builds an AccountService.
        synth::applyKeychainPolicyForLaunch(getCommandLineParameterArray(),
                                            juce::SystemStats::getEnvironmentVariable(synth::kNoKeychainEnvVar, {}),
                                            juce::SystemStats::getEnvironmentVariable(synth::kNoAudioDeviceEnvVar, {}));

        // An automation launch keeps its windows off the user's screen unless asked otherwise; this must run before
        // the first window exists.
#if AGENTSYNTH_AUTOMATION
        if (synth::isHiddenAutomationLaunch(getCommandLineParameterArray()))
            synth::testmode::hideAutomationWindows();
#endif

        // Hosted plugins write files relative to the working directory; keep that out of the
        // directory the app happened to be launched from.
        synth::enterPluginScratchDirectory();

        // An automation launch keeps all its settings and user data under its own directory (--settings-dir, else a
        // fresh temporary one); that directory is new, so there is nothing to migrate into it.
        if (synth::applySettingsDirForLaunch(getCommandLineParameterArray()).isEmpty())
            migrateLegacyUserData();

        // Apply default theme so the LnF is valid before any Component is created.
        // ThemeManager::initialise() (with appProperties) is called inside MainComponent,
        // which will restore the persisted theme and call applyTheme again.
        lookAndFeel.applyTheme(themeManager.getActiveTheme());
        juce::Desktop::getInstance().setDefaultLookAndFeel(&lookAndFeel);
        mainWindow.reset(new MainWindow(getApplicationName(), themeManager, lookAndFeel));

        // `Agent Synth.app/Contents/MacOS/Agent Synth my.agsproj` (or the OS opening a project with the app).
        // A path the OS hands over before this point waits in pendingProject_.
        if (const auto bundle = synth::projectBundleFromCommandLine(getCommandLineParameterArray()))
            pendingProject_ = *bundle;
        if (pendingProject_ != juce::File())
            openProject(std::exchange(pendingProject_, juce::File()));

        startTestModeIfRequested();
    }

    // CRITICAL ordering (spec section 7.1, constraint #1):
    // 1. Destroy all components (mainWindow = nullptr) FIRST.
    // 2. Then clear the default LnF pointer (setDefaultLookAndFeel(nullptr)).
    // 3. themeManager / lookAndFeel are data members declared BEFORE mainWindow,
    //    so they are destroyed AFTER mainWindow — they outlive the clear.
    void shutdown() override {
        // Dialogs launched async (Settings) are otherwise deleted by JUCE after this object is gone.
        synth::ui::deleteOpenModalDialogs();
        testModeServer_ = nullptr; // stop answering before the window it drives goes away
        mainWindow = nullptr;
        juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override {
        if (mainWindow != nullptr) {
            if (auto* mc = dynamic_cast<MainComponent*>(mainWindow->getContentComponent())) {
                // Asks first, quits only on Save/Discard. quit() is the LAST thing the continuation
                // does, and juce::JUCEApplicationBase::quit() only stops the dispatch loop
                // (juce_events/messages/juce_ApplicationBase.cpp: it's a one-line call to
                // MessageManager::stopDispatchLoop()) — shutdown(), which destroys mainWindow and
                // with it the MainComponent whose method invoked this continuation, runs later, after
                // main()'s call to JUCEApplicationBase::main() unwinds the now-stopped loop. Nothing
                // here touches a freed object.
                mc->guardUnsavedChanges("Quitting", [this] { quit(); });
                return;
            }
        }
        quit();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override {
        if (const auto bundle = synth::projectBundleFromCommandLine(commandLine))
            openProject(*bundle);
    }

private:
    // `--automation <port>`: the local test-mode server (docs/development/test-mode.md). Compiled in only when
    // AGENTSYNTH_AUTOMATION is on; a packaged build says so and carries on as a normal launch.
    void startTestModeIfRequested() {
        const auto args = getCommandLineParameterArray();
        if (!synth::isAutomationLaunch(args))
            return;
#if AGENTSYNTH_AUTOMATION
        const auto port = synth::automationPortFromArgs(args);
        if (!port.has_value()) {
            std::cerr << "--automation needs a port number (0 picks a free one)" << std::endl;
            return;
        }
        synth::testmode::LaunchOptions options;
        options.port = *port;
        options.keepAnimations = args.contains(synth::kAutomationMotionFlag);
        if (const auto info = synth::flagValueFromArgs(args, synth::kAutomationInfoFlag); info.isNotEmpty())
            options.infoFile = juce::File::getCurrentWorkingDirectory().getChildFile(info);
        juce::String error;
        testModeServer_ = synth::testmode::startForLaunch(options, error);
        if (testModeServer_ == nullptr)
            std::cerr << "test mode: " << error << std::endl;
#else
        juce::Logger::writeToLog("--automation is not available in this build (AGENTSYNTH_AUTOMATION is off)");
#endif
    }

    // Routes a bundle to the open window (guarded like any Open, so unsaved changes are asked about first), or
    // holds it until the window exists.
    void openProject(const juce::File& bundle) {
        if (mainWindow == nullptr) {
            pendingProject_ = bundle;
            return;
        }
        if (auto* mc = dynamic_cast<MainComponent*>(mainWindow->getContentComponent())) {
            mainWindow->toFront(!juce::ComponentPeer::hideWindowsForAutomation); // a hidden run never takes focus
            mc->openProjectFromCommandLine(bundle);
        }
    }

    // Must run before anything creates a juce::ApplicationProperties for kSettingsFolderName
    // (MainComponent's constructor does this) — otherwise JUCE creates an empty current-name
    // folder first, and migrateUserData's "already exists" guard skips the real migration.
    static void migrateLegacyUserData() {
        const auto options = synth::userSettingsOptions();

        // getDefaultFile() resolves the platform-specific settings file inside the
        // folderName directory; its grandparent is the directory that contains every
        // differently-named settings folder (past and present).
        const auto parentDir = options.getDefaultFile().getParentDirectory().getParentDirectory();

        juce::StringArray legacyNames;
        for (const auto* name : synth::branding::kLegacyFolderNames)
            legacyNames.add(name);

        const auto result = synth::migrateUserData(parentDir, synth::branding::kSettingsFolderName, legacyNames);
        if (result.migrated)
            juce::Logger::writeToLog("Migrated user settings from legacy folder \"" + result.fromName + "\" (" +
                                     juce::String(result.filesCopied) + " files)");
    }

    class MainWindow
        : public juce::DocumentWindow
        , public juce::MenuBarModel {
    public:
        MainWindow(juce::String name, synth::theme::ThemeManager& tm, synth::theme::AppLookAndFeel& lf)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                 juce::ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(tm, lf), true);
            if (auto* mc = dynamic_cast<MainComponent*>(getContentComponent())) {
                mc->onDocumentTitleChanged = [this](const juce::String& title) {
                    setName(title + " - " + JUCEApplication::getInstance()->getApplicationName());
                };
                // Seeds the title before any save/dirty event has fired — getCurrentPatchName()
                // starts as "Default", matching the window's pre-existing untitled state.
                mc->onDocumentTitleChanged(mc->getCurrentPatchName());

                // Kick off the eager background scan here — the real app's ONE call site —
                // rather than from MainComponent's own constructor, so every test and every plugin
                // editor that builds a MainComponent stays exactly as scan-free as before. Posting
                // the child processes happens on PluginScanService's own background thread; this
                // call itself only flips an atomic and (at most) spawns that thread, so it is not on
                // this window's construction critical path.
                if (!synth::isAutomationLaunch(JUCEApplication::getCommandLineParameterArray()))
                    mc->maybeStartEagerPluginScan();

                // This is the real app's ONE construction site for MainComponent
                // (every test builds one directly and never reaches here) — opt both detach hosts
                // into actually creating a native window on detach. See
                // DetachablePanelHost::setCreatesNativeWindows()'s doc comment.
                mc->getBottomDock().getTimelineHost().setCreatesNativeWindows(true);
                mc->getBottomDock().getMixerHost().setCreatesNativeWindows(true);
                mc->getBottomDock().getMidiRemoteHost().setCreatesNativeWindows(true);

                // Same reasoning, for the hosted-plugin "Open Editor" window. See
                // HostedPluginWindowManager::setCreatesNativeWindows()'s doc comment.
                mc->getPluginWindowManager().setCreatesNativeWindows(true);
            }

#if JUCE_IOS || JUCE_ANDROID
            setFullScreen(true);
#else
            setResizable(true, true);
            // Hard platform floor on the DocumentWindow (the Metrics::minWindowWidth token
            // drives layout math only; this is the actual minimum the OS will allow).
            setResizeLimits(480, 400, 8192, 8192);
            centreWithSize(1600, 900);
#endif

#if JUCE_MAC
            setMacMainMenu(this);
#else
            setMenuBar(this);
#endif
            setVisible(true);
        }

        ~MainWindow() override {
#if JUCE_MAC
            setMacMainMenu(nullptr);
#else
            setMenuBar(nullptr);
#endif
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

        // With nothing focused (right after launch, or once an in-place editor closes) a key lands on this window,
        // not on a component under MainComponent; forward it so Cmd+N and the like still run.
        bool keyPressed(const juce::KeyPress& key) override {
            return synth::forwardWindowKeyToMainComponent(getContentComponent(), key);
        }

        juce::StringArray getMenuBarNames() override { return {"File", "Edit", "Help"}; }

        juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String&) override {
            juce::PopupMenu menu;
            if (auto* mc = dynamic_cast<MainComponent*>(getContentComponent())) {
                auto& cm = mc->getCommandManager();
                if (menuIndex == 0) {
                    menu.addCommandItem(&cm, AppCommands::savePreset);
                    menu.addCommandItem(&cm, AppCommands::saveProjectAs);
                    menu.addSeparator();
                    menu.addCommandItem(&cm, AppCommands::exportPatchOnly);
                    menu.addCommandItem(&cm, AppCommands::exportAudio);
                    menu.addCommandItem(&cm, AppCommands::exportStems);
                    menu.addCommandItem(&cm, AppCommands::exportMidi);
                    menu.addCommandItem(&cm, AppCommands::collectAndArchive);
                    menu.addSeparator();
                    // Project and patch open as separate menu items - Open Project (.agsproj
                    // bundle, the rebindable Cmd+O) and Open Patch (.json preset, menu-only). addCommandItem
                    // pulls each label + keypress from getCommandInfo.
                    menu.addCommandItem(&cm, AppCommands::openProject);
                    menu.addCommandItem(&cm, AppCommands::openPreset);
                    menu.addSeparator();
                    menu.addCommandItem(&cm, AppCommands::openSettings);
                } else if (menuIndex == 1) {
                    menu.addCommandItem(&cm, AppCommands::undo);
                    menu.addCommandItem(&cm, AppCommands::redo);
                } else if (menuIndex == 2) {
                    menu.addCommandItem(&cm, AppCommands::showWelcomeScreen);
                    menu.addCommandItem(&cm, AppCommands::whatsNew);
#if JUCE_MAC || JUCE_WINDOWS
                    menu.addSeparator();
                    menu.addCommandItem(&cm, AppCommands::checkForUpdates);
#endif
                    menu.addSeparator();
                    menu.addCommandItem(&cm, AppCommands::contribute);
                    menu.addCommandItem(&cm, AppCommands::joinDiscord);
                }
            }
            return menu;
        }

        void menuItemSelected(int, int) override {}

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    // Declaration order matters: themeManager and lookAndFeel are declared BEFORE mainWindow
    // so they are constructed first and destroyed LAST (after mainWindow). This guarantees
    // the LnF object outlives every Component — the classic JUCE shutdown-crash guard.
    synth::theme::ThemeManager themeManager;
    synth::theme::AppLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<synth::testmode::TestModeServer> testModeServer_;
    juce::File pendingProject_; // a project the OS handed over before the window existed
};

//==============================================================================
// Entry point.
//
// Hand-rolled rather than START_JUCE_APPLICATION so the out-of-process plugin scan can be
// intercepted BEFORE the app object exists. A scan launches this binary once per candidate plugin;
// if each of those spun up a JUCEApplication first, macOS would bounce a Dock icon per plugin and
// every child would pay for an NSApplication it never uses. The macro's two halves are used
// verbatim — this differs from it only in the `if` below.
//
// Windows GUI builds have no argv here, so they keep the macro's WinMain and intercept at the top of
// initialise() instead (see SYNTH_HAS_ARGV_MAIN above).
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wmissing-prototypes")
JUCE_CREATE_APPLICATION_DEFINE(AppApplication)

#if SYNTH_HAS_ARGV_MAIN
int main(int argc, char* argv[]) {
    juce::StringArray args;
    for (int i = 0; i < argc; ++i)
        args.add(juce::String::fromUTF8(argv[i]));

    juce::String scanXml;
    if (const auto exitCode = synth::runPluginScanChildMode(args, scanXml, /*suppressCrashDialog=*/true)) {
        emitPluginScanChildOutput(scanXml);
        return *exitCode;
    }

    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
    return juce::JUCEApplicationBase::main(argc, (const char**)argv);
}
#else
JUCE_MAIN_FUNCTION_DEFINITION
#endif
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
