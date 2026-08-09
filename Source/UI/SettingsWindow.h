#pragma once

#include "../AI/AIIntegrationService.h"
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
    SettingsWindow(juce::AudioDeviceManager& deviceManager, juce::ApplicationProperties& appProperties,
                   synth::AIIntegrationService& aiService, synth::AIChatComponent& aiChatComponent,
                   ShortcutManager& shortcutManager, synth::theme::ThemeManager& themeManager,
                   GraphEditor* graphEditor);
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
