#include "Plugin/Hosting/HostedPluginSleepGuard.h"
#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"

// Concern: the Plugins group -- "Unload plugins while the Mac sleeps" (docs/architecture/plugin-layer.md, "When a
// plugin takes the app down"). Nothing is pushed from here: the app's HostedPluginSleepGuard reads
// synth::kUnloadPluginsDuringSleepKey from the user settings at each sleep, so a toggle applies at once. Chained
// between the MIDI Remote group and the Privacy group.

bool PreferencesSettingsTab::isUnloadPluginsDuringSleepEnabled() const {
    return unloadPluginsDuringSleepToggle.getToggleState();
}

void PreferencesSettingsTab::setUnloadPluginsDuringSleepEnabled(bool enabled) {
    unloadPluginsDuringSleepToggle.setToggleState(enabled, juce::dontSendNotification);
    persistUnloadPluginsDuringSleep(enabled);
}

void PreferencesSettingsTab::persistUnloadPluginsDuringSleep(bool enabled) {
    appProperties.getUserSettings()->setValue(synth::kUnloadPluginsDuringSleepKey, enabled);
    appProperties.getUserSettings()->saveIfNeeded();
}

void PreferencesSettingsTab::setupPluginSleepControls() {
    contentHost.addAndMakeVisible(unloadPluginsDuringSleepToggle);
    unloadPluginsDuringSleepToggle.setTitle("Unload plugins while the Mac sleeps");
    unloadPluginsDuringSleepToggle.setTooltip("Saves each hosted plugin and closes it before the Mac sleeps, then "
                                              "loads it again as it was when the Mac wakes.");
    // DEFAULT TRUE: an install that never opens this tab is protected too.
    unloadPluginsDuringSleepToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(synth::kUnloadPluginsDuringSleepKey, true),
        juce::dontSendNotification);
    unloadPluginsDuringSleepToggle.onClick = [this] {
        persistUnloadPluginsDuringSleep(unloadPluginsDuringSleepToggle.getToggleState());
    };

    contentHost.addAndMakeVisible(unloadPluginsDuringSleepHint);
    unloadPluginsDuringSleepHint.setText("Keeps a plugin that breaks on wake from taking Agent Synth down. A short "
                                         "silence after wake is expected. Turn off only if a plugin reloads badly.",
                                         juce::dontSendNotification);
    styleMutedHintLabel(unloadPluginsDuringSleepHint);
    setupUsageStatsControls(); // Chained here, the constructor is baselined
}

void PreferencesSettingsTab::layoutPluginSleepGroup(int& y, int contentWidth, bool previousGroupWasVisible,
                                                    const GroupMatchFn& groupMatches,
                                                    const SetVisibleFn& setGroupVisible) {
    enterCategory(Category::Plugins, y);
    const std::initializer_list<juce::Component*> comps = {&unloadPluginsDuringSleepToggle,
                                                           &unloadPluginsDuringSleepHint};
    const bool visible = groupMatches(comps);
    setGroupVisible(comps, visible);
    if (visible) {
        if (previousGroupWasVisible) {
            y += 10;
            dividerBounds.push_back(juce::Rectangle<int>{0, y, contentWidth, 1});
            y += 11;
        }
        unloadPluginsDuringSleepToggle.setBounds({0, y, contentWidth, 24});
        y += 24;
        unloadPluginsDuringSleepHint.setBounds({24, y, contentWidth - 24, kHintHeight});
        y += kHintHeight;
    }
    layoutUsageStatsGroup(y, contentWidth, visible || previousGroupWasVisible, groupMatches, setGroupVisible);
}
