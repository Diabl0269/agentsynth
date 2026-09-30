#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"

// Concern: the detach-mode combo (docs/mixer/panel.md#placement-and-detachable-windows) -- "When a panel opens in
// its own window: move it there (default) / show it in both places". Same "own named step, chained
// from the tail of the previous group" pattern setupMixerPlacementControls()/
// layoutMixerPlacementGroup() (PreferencesSettingsTabMixerDefaults.cpp) already establish; this
// group's own setup/layout functions are chained straight after those two.

juce::String PreferencesSettingsTab::getPanelDetachMode() const {
    return panelDetachModeCombo.getSelectedId() == kPanelDetachModeBothComboId ? "both" : "move";
}

void PreferencesSettingsTab::setPanelDetachMode(const juce::String& mode) {
    const int id = mode == "both" ? kPanelDetachModeBothComboId : kPanelDetachModeMoveComboId;
    panelDetachModeCombo.setSelectedId(id, juce::dontSendNotification);
    persistPanelDetachMode(getPanelDetachMode());
}

void PreferencesSettingsTab::persistPanelDetachMode(const juce::String& mode) {
    appProperties.getUserSettings()->setValue(kPanelDetachModeKey, mode);
    appProperties.getUserSettings()->saveIfNeeded();
}

void PreferencesSettingsTab::setupPanelDetachModeControls() {
    contentHost.addAndMakeVisible(panelDetachModeLabel);
    panelDetachModeLabel.setText("When a panel opens in its own window:", juce::dontSendNotification);
    panelDetachModeLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(panelDetachModeCombo);
    panelDetachModeCombo.addItem("Move it there", kPanelDetachModeMoveComboId);
    panelDetachModeCombo.addItem("Show it in both places", kPanelDetachModeBothComboId);
    // setPanelDetachMode also re-persists the value it just read, harmless (idempotent) and keeps
    // this to one code path -- same idiom setupMixerPlacementControls() uses.
    setPanelDetachMode(appProperties.getUserSettings()->getValue(kPanelDetachModeKey, "move"));
    panelDetachModeCombo.onChange = [this] { persistPanelDetachMode(getPanelDetachMode()); };
    setupMidiRemoteControls(); // Chained here, the constructor is baselined
}

void PreferencesSettingsTab::layoutPanelDetachModeGroup(
    int& y, int contentWidth, bool previousGroupWasVisible,
    const std::function<bool(std::initializer_list<juce::Component*>)>& groupMatches,
    const std::function<void(std::initializer_list<juce::Component*>, bool)>& setGroupVisible) {
    layoutCategory = Category::Panels;
    const std::initializer_list<juce::Component*> panelDetachModeComps = {&panelDetachModeLabel, &panelDetachModeCombo};
    const bool visible = groupMatches(panelDetachModeComps);
    setGroupVisible(panelDetachModeComps, visible);
    // The MIDI Remote group follows; chained here for the same baselined-layoutContent reason.
    const auto chainNext = [&] {
        layoutMidiRemoteGroup(y, contentWidth, visible || previousGroupWasVisible, groupMatches, setGroupVisible);
    };
    if (!visible) {
        chainNext();
        return;
    }
    if (previousGroupWasVisible) {
        // Same divider math as layoutContent's own addDivider() closure -- see
        // layoutMixerPlacementGroup()'s own comment on why this is inlined rather than shared.
        y += 10;
        dividerBounds.push_back(juce::Rectangle<int>{0, y, contentWidth, 1});
        y += 11;
    }
    juce::Rectangle<int> row(0, y, contentWidth, 24);
    panelDetachModeLabel.setBounds(row.removeFromLeft(230));
    row.removeFromLeft(4);
    panelDetachModeCombo.setBounds(row.removeFromLeft(190));
    y += 24;
    chainNext();
}
