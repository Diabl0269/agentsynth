#include "Branding.h"
#include "MainComponent.h"
#include "SettingsMigration.h"
#include "ShortcutManager.h"
#include "UI/Theme/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include <JuceHeader.h>

class AppApplication : public juce::JUCEApplication {
public:
    AppApplication() {}

    const juce::String getApplicationName() override { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override {
        juce::ignoreUnused(commandLine);

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

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted(const juce::String& commandLine) override { juce::ignoreUnused(commandLine); }

private:
    // Must run before anything creates a juce::ApplicationProperties for kSettingsFolderName
    // (MainComponent's constructor does this) — otherwise JUCE creates an empty current-name
    // folder first, and migrateUserData's "already exists" guard skips the real migration.
    static void migrateLegacyUserData() {
        juce::PropertiesFile::Options options;
        options.applicationName = synth::branding::kProductName;
        options.folderName = synth::branding::kSettingsFolderName;
        options.filenameSuffix = "settings";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat = juce::PropertiesFile::storeAsXML;

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
START_JUCE_APPLICATION(AppApplication)
