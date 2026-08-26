#pragma once

#include "../AI/AIIntegrationService.h"
#include "../AI/AccountService.h"
#include "AIChatComponent.h"
#include "GraphEditor.h"
#include "Theme/ThemeManager.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

class ShortcutManager;

class SettingsWindow
    : public juce::Component
    , private juce::ChangeListener {
public:
    // showAudioTab=false omits the audio-device selector. Used by the plugin build: the host owns
    // the audio device, so an AudioDeviceSelectorComponent there would be inert at best and, if the
    // user touched it, would try to open hardware out from under the host.
    //
    // accountService is nullable, defaulting to nullptr — same "invisible/inert until attached"
    // contract as AccountRow/PlanBadge::setAccountService(nullptr) — so every existing call site
    // (including every SettingsWindowTests.cpp test) keeps compiling and gets the signed-out-only
    // AI tab (P6-7's prompt-learning toggle disabled) rather than a required dependency.
    // initialTabName: when non-empty and it matches a tab's name (by exact TabbedComponent tab
    // name), that tab is selected on construction instead of the persisted "settingsTab"
    // preference. Empty (the default) keeps the existing persisted-tab behaviour.
    SettingsWindow(juce::AudioDeviceManager& deviceManager, juce::ApplicationProperties& appProperties,
                   synth::AIIntegrationService& aiService, synth::AIChatComponent& aiChatComponent,
                   ShortcutManager& shortcutManager, synth::theme::ThemeManager& themeManager, GraphEditor* graphEditor,
                   synth::AccountService* accountService = nullptr, bool showAudioTab = true,
                   std::function<void(bool)> onTimelineFeatureToggled = nullptr, juce::String initialTabName = {});
    ~SettingsWindow() override;

    void resized() override;

    // Testing hooks
    int getNumTabs() const { return tabs.getNumTabs(); }
    juce::String getTabName(int index) const { return tabs.getTabNames()[index]; }
    int getCurrentTabIndex() const { return tabs.getCurrentTabIndex(); }
    juce::TabbedComponent& getTabs() { return tabs; }

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::ApplicationProperties& appProperties;
    synth::theme::ThemeManager& themeManager;
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsWindow)
};
