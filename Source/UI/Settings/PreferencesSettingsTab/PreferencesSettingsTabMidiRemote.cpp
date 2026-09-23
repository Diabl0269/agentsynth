#include "MidiRemote/MidiRemotePreferences.h"
#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"

// Concern: the MIDI Remote group -- Default takeover and the mapped-control badge switch
// (FRO136, docs/control/midi-remote-ui.md#settings). The keys live in UserSettings.h; MainComponent
// re-reads them on every settings-file change (applyMidiRemotePreferences), so nothing here pushes.

namespace {

constexpr int kTakeoverJumpComboId = 1;
constexpr int kTakeoverPickupComboId = 2;
constexpr int kTakeoverScaleComboId = 3;

int comboIdFromTakeover(synth::Takeover takeover) {
    switch (takeover) {
    case synth::Takeover::jump:
        return kTakeoverJumpComboId;
    case synth::Takeover::pickup:
        return kTakeoverPickupComboId;
    default:
        return kTakeoverScaleComboId;
    }
}

} // namespace

synth::Takeover PreferencesSettingsTab::getMidiRemoteDefaultTakeover() const {
    switch (midiRemoteTakeoverCombo.getSelectedId()) {
    case kTakeoverJumpComboId:
        return synth::Takeover::jump;
    case kTakeoverPickupComboId:
        return synth::Takeover::pickup;
    default:
        return synth::Takeover::scale;
    }
}

void PreferencesSettingsTab::setMidiRemoteDefaultTakeover(synth::Takeover takeover) {
    midiRemoteTakeoverCombo.setSelectedId(comboIdFromTakeover(takeover), juce::dontSendNotification);
    persistMidiRemoteDefaultTakeover(getMidiRemoteDefaultTakeover());
}

bool PreferencesSettingsTab::isMidiRemoteShowBadgesEnabled() const {
    return midiRemoteShowBadgesToggle.getToggleState();
}

void PreferencesSettingsTab::setMidiRemoteShowBadgesEnabled(bool enabled) {
    midiRemoteShowBadgesToggle.setToggleState(enabled, juce::dontSendNotification);
    persistMidiRemoteShowBadges(enabled);
}

void PreferencesSettingsTab::persistMidiRemoteDefaultTakeover(synth::Takeover takeover) {
    appProperties.getUserSettings()->setValue(synth::kMidiRemoteDefaultTakeoverSettingKey,
                                              synth::midi::takeoverSettingValue(takeover));
    appProperties.getUserSettings()->saveIfNeeded();
}

void PreferencesSettingsTab::persistMidiRemoteShowBadges(bool enabled) {
    appProperties.getUserSettings()->setValue(synth::kMidiRemoteShowBadgesSettingKey, enabled);
    appProperties.getUserSettings()->saveIfNeeded();
}

void PreferencesSettingsTab::setupMidiRemoteControls() {
    contentHost.addAndMakeVisible(midiRemoteTakeoverLabel);
    midiRemoteTakeoverLabel.setText("MIDI Remote default takeover:", juce::dontSendNotification);
    midiRemoteTakeoverLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(midiRemoteTakeoverCombo);
    midiRemoteTakeoverCombo.addItem("Jump", kTakeoverJumpComboId);
    midiRemoteTakeoverCombo.addItem("Pick-up", kTakeoverPickupComboId);
    midiRemoteTakeoverCombo.addItem("Scale", kTakeoverScaleComboId);
    midiRemoteTakeoverCombo.setTooltip("What a hardware knob does when it is not where the parameter is. "
                                       "Assignments set to Default follow this.");
    // Read back through the setter, like the sibling groups: one code path, idempotent write.
    setMidiRemoteDefaultTakeover(synth::midi::loadDefaultTakeover(*appProperties.getUserSettings()));
    midiRemoteTakeoverCombo.onChange = [this] { persistMidiRemoteDefaultTakeover(getMidiRemoteDefaultTakeover()); };

    contentHost.addAndMakeVisible(midiRemoteShowBadgesToggle);
    setMidiRemoteShowBadgesEnabled(synth::midi::loadShowBadges(*appProperties.getUserSettings()));
    midiRemoteShowBadgesToggle.onClick = [this] {
        persistMidiRemoteShowBadges(midiRemoteShowBadgesToggle.getToggleState());
    };
}

void PreferencesSettingsTab::layoutMidiRemoteGroup(
    int& y, int contentWidth, bool previousGroupWasVisible,
    const std::function<bool(std::initializer_list<juce::Component*>)>& groupMatches,
    const std::function<void(std::initializer_list<juce::Component*>, bool)>& setGroupVisible) {
    const std::initializer_list<juce::Component*> comps = {&midiRemoteTakeoverLabel, &midiRemoteTakeoverCombo,
                                                           &midiRemoteShowBadgesToggle};
    const bool visible = groupMatches(comps);
    setGroupVisible(comps, visible);
    if (!visible)
        return;
    if (previousGroupWasVisible) {
        y += 10;
        dividerBounds.push_back(juce::Rectangle<int>{0, y, contentWidth, 1});
        y += 11;
    }
    juce::Rectangle<int> row(0, y, contentWidth, 24);
    midiRemoteTakeoverLabel.setBounds(row.removeFromLeft(190));
    row.removeFromLeft(4);
    midiRemoteTakeoverCombo.setBounds(row.removeFromLeft(140));
    y += 28;
    midiRemoteShowBadgesToggle.setBounds(0, y, contentWidth, 24);
    y += 24;
}
