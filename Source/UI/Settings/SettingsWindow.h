#pragma once

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AccountService.h"
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintOverlay.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/ArrowKeyNavigation.h"
#include "UI/Layout/TabSwitchKeys.h"
#include "UI/Settings/SettingsTabs.h"
#include "UI/Theme/ThemeManager.h"
#include <functional>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class ShortcutManager;
class PreferencesSettingsTab;

class SettingsWindow
    : public juce::Component
    , private juce::ChangeListener
    , private juce::FocusChangeListener
    , private juce::Timer {
public:
    // Size the window opens at, and the least it can be dragged down to: the tabs' pinned rows (the
    // Preferences picker row above all) are laid out to fit at this width.
    static constexpr int kDefaultWidth = 500;
    static constexpr int kDefaultHeight = 450;
    static constexpr int kMinWidth = kDefaultWidth;
    static constexpr int kMinHeight = 300;

    // showAudioTab=false omits the audio-device selector. Used by the plugin build: the host owns
    // the audio device, so an AudioDeviceSelectorComponent there would be inert at best and, if the
    // user touched it, would try to open hardware out from under the host.
    //
    // accountService is nullable, defaulting to nullptr — same "invisible/inert until attached"
    // contract as AccountRow/PlanBadge::setAccountService(nullptr) — so every existing call site
    // (including every SettingsWindowTests.cpp test) keeps compiling and gets the signed-out-only
    // AI tab (prompt-learning toggle disabled) rather than a required dependency.
    // initialTabName: when non-empty and it matches a tab's name (by exact TabbedComponent tab
    // name), that tab is selected on construction instead of the remembered tab. Empty (the default)
    // opens the remembered tab: its name is saved when the window closes, so it survives the Audio tab
    // being present in the app and absent in the plugin.
    SettingsWindow(juce::AudioDeviceManager& deviceManager, juce::ApplicationProperties& appProperties,
                   synth::AIIntegrationService& aiService, synth::AIChatComponent& aiChatComponent,
                   ShortcutManager& shortcutManager, synth::theme::ThemeManager& themeManager, GraphEditor* graphEditor,
                   synth::AccountService* accountService = nullptr, bool showAudioTab = true,
                   juce::String initialTabName = {}, std::vector<juce::String> midiRemoteDeviceNames = {});
    ~SettingsWindow() override;

    void resized() override;

    // Routes the Preferences tab's "What we collect" link through the owner's URL opener.
    void setUrlOpener(std::function<void(const juce::URL&)> opener);

    // Escape closes the window, from any control inside it (a text field passes it up). Fires
    // onRequestClose when set; otherwise closes the juce::DialogWindow hosting this content.
    // Cmd+1..9 opens the Nth tab (a number past the last tab does nothing). Text fields in the tabs offer
    // this key to the window before they type. Holding Cmd shows each tab's Cmd+N as a hint badge.
    bool keyPressed(const juce::KeyPress& key) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;
    std::function<void()> onRequestClose;

    // Testing hooks
    int getNumTabs() const { return tabs.getNumTabs(); }
    juce::String getTabName(int index) const { return tabs.getTabNames()[index]; }
    int getCurrentTabIndex() const { return tabs.getCurrentTabIndex(); }
    juce::TabbedComponent& getTabs() { return tabs; }
    // The list-style arrow keys attached to a tab's content (one per tab, in tab order).
    synth::ui::ArrowKeyNavigation& getArrowKeysForTest(int tabIndex) {
        return *arrowKeys[static_cast<size_t>(tabIndex)];
    }

    // Puts keyboard focus on the tab strip (its one Tab stop), where Left / Right / Home / End switch
    // tab and Return or Tab go into the tab. Happens by itself whenever the window it sits in takes
    // keyboard focus (on opening, and each time the window is brought to the front again); call it to
    // return to the tab strip.
    void focusCurrentTab() { tabs.focusTabStrip(); }
    // The reaction to focus landing somewhere: when `focused` is the window hosting this content, which
    // has no control to type into, moves focus to the tab strip and returns true.
    bool redirectWindowFocusToTabStrip(juce::Component* focused);
    juce::Component& getTabStripFocus() noexcept { return tabs.getStripFocus(); }
    SettingsTabs& getSettingsTabsForTest() noexcept { return tabs; }
    synth::ui::ShortcutHintOverlay& getShortcutHintsForTest() noexcept { return *shortcutHints; }
    synth::ui::TabSwitchKeys& getTabSwitchKeysForTest() { return tabSwitchKeys; }
    juce::Component* getCurrentTabButton() const {
        return tabs.getTabbedButtonBar().getTabButton(tabs.getCurrentTabIndex());
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    // The tab-switching keys; false for any other key.
    bool handleTabKey(const juce::KeyPress& key);
    void attachShortcutHints();
    void timerCallback() override;
    void globalFocusChanged(juce::Component* focused) override { redirectWindowFocusToTabStrip(focused); }

    juce::ApplicationProperties& appProperties;
    PreferencesSettingsTab* preferencesTab = nullptr; // owned by `tabs`
    ShortcutManager& shortcutManager;
    synth::theme::ThemeManager& themeManager;
    SettingsTabs tabs{juce::TabbedButtonBar::TabsAtTop};
    // Declared after `tabs` so it is destroyed (and detached from the text fields) before them.
    synth::ui::TabSwitchKeys tabSwitchKeys{[this](const juce::KeyPress& key) { return handleTabKey(key); }};
    // Declared after `tabs` so each is destroyed before the content it listens on.
    std::vector<std::unique_ptr<synth::ui::ArrowKeyNavigation>> arrowKeys;
    // Last, so it goes first: it is a child of this window and listens on it.
    std::unique_ptr<synth::ui::ShortcutHintOverlay> shortcutHints;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsWindow)
};
