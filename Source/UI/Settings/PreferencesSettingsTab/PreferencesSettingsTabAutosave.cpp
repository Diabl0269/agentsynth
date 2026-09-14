#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"

// Concern: autosave enabled/interval/backup-count preferences (P8-4).

bool PreferencesSettingsTab::isAutosaveEnabled() const { return autosaveEnabledToggle.getToggleState(); }

void PreferencesSettingsTab::setAutosaveEnabled(bool enabled) {
    autosaveEnabledToggle.setToggleState(enabled, juce::dontSendNotification);
    persistAutosaveEnabled(enabled);
}

void PreferencesSettingsTab::persistAutosaveEnabled(bool enabled) {
    appProperties.getUserSettings()->setValue(kAutosaveEnabledKey, enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    // No live push from here: MainComponent::maybeAutosave reads the key directly on every tick, the
    // same "read at use time" idiom persistDoubleClickSpansLocators documents above — there is
    // nothing to push, since the very next tick already sees the new value.
}

int PreferencesSettingsTab::getAutosaveIntervalMinutes() const {
    return autosaveIntervalEditor.getText().getIntValue();
}

void PreferencesSettingsTab::setAutosaveIntervalMinutes(int minutes) {
    const int clamped = juce::jlimit(1, 120, minutes);
    autosaveIntervalEditor.setText(juce::String(clamped), juce::dontSendNotification);
    persistAutosaveIntervalMinutes(clamped);
}

void PreferencesSettingsTab::persistAutosaveIntervalMinutes(int minutes) {
    appProperties.getUserSettings()->setValue(kAutosaveIntervalMinutesKey, minutes);
    appProperties.getUserSettings()->saveIfNeeded();
}

int PreferencesSettingsTab::getAutosaveBackupCount() const { return autosaveBackupCountEditor.getText().getIntValue(); }

void PreferencesSettingsTab::setAutosaveBackupCount(int count) {
    const int clamped = juce::jlimit(0, 50, count);
    autosaveBackupCountEditor.setText(juce::String(clamped), juce::dontSendNotification);
    persistAutosaveBackupCount(clamped);
}

void PreferencesSettingsTab::persistAutosaveBackupCount(int count) {
    appProperties.getUserSettings()->setValue(kAutosaveBackupCountKey, count);
    appProperties.getUserSettings()->saveIfNeeded();
}
