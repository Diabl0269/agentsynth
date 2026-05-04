#include "MainComponent.h"
#include "ShortcutManager.h"
#include <JuceHeader.h>

class AgentSynthApplication : public juce::JUCEApplication {
public:
    AgentSynthApplication() {}

    const juce::String getApplicationName() override { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override {
        juce::ignoreUnused(commandLine);
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override { mainWindow = nullptr; }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted(const juce::String& commandLine) override { juce::ignoreUnused(commandLine); }

    class MainWindow
        : public juce::DocumentWindow
        , public juce::MenuBarModel {
    public:
        MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                 juce::ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);

#if JUCE_IOS || JUCE_ANDROID
            setFullScreen(true);
#else
            setResizable(true, true);
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
                    menu.addCommandItem(&cm, AgentSynthCommands::savePreset);
                    menu.addCommandItem(&cm, AgentSynthCommands::openPreset);
                    menu.addSeparator();
                    menu.addCommandItem(&cm, AgentSynthCommands::openSettings);
                } else if (menuIndex == 1) {
                    menu.addCommandItem(&cm, AgentSynthCommands::undo);
                    menu.addCommandItem(&cm, AgentSynthCommands::redo);
                }
            }
            return menu;
        }

        void menuItemSelected(int, int) override {}

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION(AgentSynthApplication)
