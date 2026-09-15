#include "Mixer/TrackPresetManager.h"
#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"

// Concern: Mixer -> per-type default track preset combos (FRO13, P9-7, docs/mixer.md §5.7/§7 D3).

// Populates `combo` with the "Factory Default" sentinel (kMixerDefaultPresetFactoryComboId) plus
// every saved `kind` preset (kMixerDefaultPresetComboIdBase + index) — same snapshot-at-populate-
// time posture the "+ Track" menu's own preset submenus take, since this combo is populated once
// at construction rather than re-collected live. Declared extern in
// PreferencesSettingsTabInternal.h (the constructor, in PreferencesSettingsTabLifecycle.cpp, is
// where it's actually called) — same "one definition, several callers" pattern as comboIdFromMode.
void populateMixerDefaultPresetCombo(juce::ComboBox& combo, synth::TrackPresetKind kind) {
    combo.clear(juce::dontSendNotification);
    combo.addItem(synth::TrackPresetManager::kFactoryDefaultSentinel, kMixerDefaultPresetFactoryComboId);
    auto dir = synth::TrackPresetManager::getDefaultTrackPresetsDirectory();
    const auto presets = synth::TrackPresetManager::listTrackPresets(dir, kind);
    for (int i = 0; i < presets.size(); ++i)
        combo.addItem(presets[i].name, kMixerDefaultPresetComboIdBase + i);
    combo.setSelectedId(kMixerDefaultPresetFactoryComboId, juce::dontSendNotification);
}

namespace {

// Selects `presetName` in `combo` if it is still one of the listed rows; leaves the current
// selection alone otherwise (a deleted preset silently falls through to whatever the combo already
// shows, same as MainComponent's own "name resolves to nothing -> fall through" default-consulting
// branch does for the actual chain build).
void selectMixerDefaultPreset(juce::ComboBox& combo, const juce::String& presetName) {
    if (presetName.isEmpty()) {
        combo.setSelectedId(kMixerDefaultPresetFactoryComboId, juce::dontSendNotification);
        return;
    }
    for (int i = 0; i < combo.getNumItems(); ++i) {
        if (combo.getItemText(i) == presetName) {
            combo.setSelectedId(combo.getItemId(i), juce::dontSendNotification);
            return;
        }
    }
}

} // namespace

void PreferencesSettingsTab::persistMixerDefaultTrackPresetAudio(const juce::String& presetName) {
    appProperties.getUserSettings()->setValue(kMixerDefaultTrackPresetAudioKey, presetName);
    appProperties.getUserSettings()->saveIfNeeded();
}

void PreferencesSettingsTab::persistMixerDefaultTrackPresetInstrument(const juce::String& presetName) {
    appProperties.getUserSettings()->setValue(kMixerDefaultTrackPresetInstrumentKey, presetName);
    appProperties.getUserSettings()->saveIfNeeded();
}

juce::String PreferencesSettingsTab::getMixerDefaultTrackPresetAudio() const {
    return mixerDefaultTrackPresetAudioCombo.getSelectedId() <= kMixerDefaultPresetFactoryComboId
               ? juce::String()
               : mixerDefaultTrackPresetAudioCombo.getText();
}

void PreferencesSettingsTab::setMixerDefaultTrackPresetAudio(const juce::String& presetName) {
    selectMixerDefaultPreset(mixerDefaultTrackPresetAudioCombo, presetName);
    persistMixerDefaultTrackPresetAudio(getMixerDefaultTrackPresetAudio());
}

juce::String PreferencesSettingsTab::getMixerDefaultTrackPresetInstrument() const {
    return mixerDefaultTrackPresetInstrumentCombo.getSelectedId() <= kMixerDefaultPresetFactoryComboId
               ? juce::String()
               : mixerDefaultTrackPresetInstrumentCombo.getText();
}

void PreferencesSettingsTab::setMixerDefaultTrackPresetInstrument(const juce::String& presetName) {
    selectMixerDefaultPreset(mixerDefaultTrackPresetInstrumentCombo, presetName);
    persistMixerDefaultTrackPresetInstrument(getMixerDefaultTrackPresetInstrument());
}
