#pragma once

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AccountService.h"
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Settings/SettingsTabs.h"
#include "UI/Theme/ThemeManager.h"
#include <functional>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class ShortcutManager;

class SettingsWindow
    : public juce::Component
    , private juce::ChangeListener
    , private juce::FocusChangeListener {
public:
    // showAudioTab=false omits the audio-device selector. Used by the plugin build: the host owns
    // the audio device, so an AudioDeviceSelectorComponent there would be inert at best and, if the
    // user touched it, would try to open hardware out from under the host.
    //
    // accountService is nullable, defaulting to nullptr — same "invisible/inert until attached"
    // contract as AccountRow/PlanBadge::setAccountService(nullptr) — so every existing call site
    // (including every SettingsWindowTests.cpp test) keeps compiling and gets the signed-out-only
    // AI tab (prompt-learning toggle disabled) rather than a required dependency.
    // initialTabName: when non-empty and it matches a tab's name (by exact TabbedComponent tab
    // name), that tab is selected on construction instead of the persisted "settingsTab"
    // preference. Empty (the default) keeps the existing persisted-tab behaviour.
    SettingsWindow(juce::AudioDeviceManager& deviceManager, juce::ApplicationProperties& appProperties,
                   synth::AIIntegrationService& aiService, synth::AIChatComponent& aiChatComponent,
                   ShortcutManager& shortcutManager, synth::theme::ThemeManager& themeManager, GraphEditor* graphEditor,
                   synth::AccountService* accountService = nullptr, bool showAudioTab = true,
                   juce::String initialTabName = {}, std::vector<juce::String> midiRemoteDeviceNames = {});
    ~SettingsWindow() override;

    void resized() override;

    // Escape closes the window, from any control inside it (a text field passes it up). Fires
    // onRequestClose when set; otherwise closes the juce::DialogWindow hosting this content.
    bool keyPressed(const juce::KeyPress& key) override;
    std::function<void()> onRequestClose;

    // Testing hooks
    int getNumTabs() const { return tabs.getNumTabs(); }
    juce::String getTabName(int index) const { return tabs.getTabNames()[index]; }
    int getCurrentTabIndex() const { return tabs.getCurrentTabIndex(); }
    juce::TabbedComponent& getTabs() { return tabs; }

    // Puts keyboard focus on the open tab's button, where Tab, Space and Return act on the tab strip.
    // Happens by itself whenever the window it sits in takes keyboard focus (on opening, and each time
    // the window is brought to the front again); call it to return to the tab strip.
    void focusCurrentTab() { tabs.focusCurrentTabButton(); }
    // The reaction to focus landing somewhere: when `focused` is the window hosting this content, which
    // has no control to type into, moves focus to the open tab's button and returns true.
    bool redirectWindowFocusToTabStrip(juce::Component* focused);
    juce::Component* getCurrentTabButton() const {
        return tabs.getTabbedButtonBar().getTabButton(tabs.getCurrentTabIndex());
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void globalFocusChanged(juce::Component* focused) override { redirectWindowFocusToTabStrip(focused); }

    juce::ApplicationProperties& appProperties;
    synth::theme::ThemeManager& themeManager;
    SettingsTabs tabs{juce::TabbedButtonBar::TabsAtTop};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsWindow)
};
