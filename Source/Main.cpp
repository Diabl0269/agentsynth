#include "MainComponent.h"
#include "ShortcutManager.h"
#include "UI/Theme/GravisynthLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include <JuceHeader.h>

class GravisynthApplication : public juce::JUCEApplication {
public:
    GravisynthApplication() {}

    const juce::String getApplicationName() override { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override {
        juce::ignoreUnused(commandLine);
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

    class MainWindow
        : public juce::DocumentWindow
        , public juce::MenuBarModel {
    public:
        MainWindow(juce::String name, gsynth::theme::ThemeManager& tm, gsynth::theme::GravisynthLookAndFeel& lf)
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

        juce::StringArray getMenuBarNames() override { return {"File", "Edit"}; }

        juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String&) override {
            juce::PopupMenu menu;
            if (auto* mc = dynamic_cast<MainComponent*>(getContentComponent())) {
                auto& cm = mc->getCommandManager();
                if (menuIndex == 0) {
                    menu.addCommandItem(&cm, GravisynthCommands::savePreset);
                    menu.addCommandItem(&cm, GravisynthCommands::openPreset);
                    menu.addSeparator();
                    menu.addCommandItem(&cm, GravisynthCommands::openSettings);
                } else if (menuIndex == 1) {
                    menu.addCommandItem(&cm, GravisynthCommands::undo);
                    menu.addCommandItem(&cm, GravisynthCommands::redo);
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
    gsynth::theme::ThemeManager themeManager;
    gsynth::theme::GravisynthLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION(GravisynthApplication)
