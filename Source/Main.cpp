#include "Branding.h"
#include "MainComponent.h"
#include "Plugin/Hosting/PluginScanService.h"
#include "SettingsMigration.h"
#include "ShortcutManager.h"
#include "UI/Theme/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include "UserSettings.h"
#include <JuceHeader.h>
#include <iostream>

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

    void initialise(const juce::String& commandLine) override {
        juce::ignoreUnused(commandLine);

#if !SYNTH_HAS_ARGV_MAIN
        // Windows GUI fallback for the out-of-process plugin scan. Everywhere else main()
        // below short-circuits before the app object is ever constructed; here JUCE owns WinMain and
        // this is the earliest hook there is, so the child pays for an app spin-up. It still creates
        // no window, no engine and no settings file — this runs before all of that.
        {
            juce::String scanXml;
            if (const auto exitCode = synth::runPluginScanChildMode(getCommandLineParameterArray(), scanXml)) {
                emitPluginScanChildOutput(scanXml);
                setApplicationReturnValue(*exitCode);
                quit();
                return;
            }
        }
#endif

        migrateLegacyUserData();

        // Apply default theme so the LnF is valid before any Component is created.
        // ThemeManager::initialise() (with appProperties) is called inside MainComponent,
        // which will restore the persisted theme and call applyTheme again.
        lookAndFeel.applyTheme(themeManager.getActiveTheme());
        juce::Desktop::getInstance().setDefaultLookAndFeel(&lookAndFeel);
        mainWindow.reset(new MainWindow(getApplicationName(), themeManager, lookAndFeel));
    }

    // CRITICAL ordering (spec section 7.1, constraint #1):
    // 1. Destroy all components (mainWindow = nullptr) FIRST.
    // 2. Then clear the default LnF pointer (setDefaultLookAndFeel(nullptr)).
    // 3. themeManager / lookAndFeel are data members declared BEFORE mainWindow,
    //    so they are destroyed AFTER mainWindow — they outlive the clear.
    void shutdown() override {
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

    void anotherInstanceStarted(const juce::String& commandLine) override { juce::ignoreUnused(commandLine); }

private:
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

        juce::StringArray getMenuBarNames() override {
#if JUCE_MAC
            return {"File", "Edit", "Help"};
#else
            return {"File", "Edit"};
#endif
        }

        juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String&) override {
            juce::PopupMenu menu;
            if (auto* mc = dynamic_cast<MainComponent*>(getContentComponent())) {
                auto& cm = mc->getCommandManager();
                if (menuIndex == 0) {
                    menu.addCommandItem(&cm, AppCommands::savePreset);
                    menu.addCommandItem(&cm, AppCommands::saveProjectAs);
                    menu.addSeparator();
                    menu.addCommandItem(&cm, AppCommands::exportPatchOnly);
                    menu.addSeparator();
                    menu.addCommandItem(&cm, AppCommands::openPreset);
                    menu.addSeparator();
                    menu.addCommandItem(&cm, AppCommands::openSettings);
                } else if (menuIndex == 1) {
                    menu.addCommandItem(&cm, AppCommands::undo);
                    menu.addCommandItem(&cm, AppCommands::redo);
                }
#if JUCE_MAC
                else if (menuIndex == 2) {
                    menu.addCommandItem(&cm, AppCommands::checkForUpdates);
                }
#endif
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
    if (const auto exitCode = synth::runPluginScanChildMode(args, scanXml)) {
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
