#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"
#include "UI/Chrome/CanvasToolbar/ToolbarLabelsPreference.h"

// Concern: the "Show button labels" row (docs/layout/chrome.md#toolbar; Panels & Windows). Off hides the captions
// under every top-bar and canvas-toolbar button, leaving only the icons; a button's tooltip still names it and its
// key. Nothing is pushed from here: MainComponent re-reads synth::ui::kShowToolbarLabelsKey on every settings write
// and re-applies the labels, so a toggle applies at once. Chained between the info tooltips row and the MIDI Remote
// group.

bool PreferencesSettingsTab::isShowToolbarLabelsEnabled() const { return showToolbarLabelsToggle.getToggleState(); }

void PreferencesSettingsTab::setShowToolbarLabelsEnabled(bool enabled) {
    showToolbarLabelsToggle.setToggleState(enabled, juce::dontSendNotification);
    persistShowToolbarLabels(enabled);
}

void PreferencesSettingsTab::persistShowToolbarLabels(bool enabled) {
    appProperties.getUserSettings()->setValue(synth::ui::kShowToolbarLabelsKey, enabled);
    appProperties.getUserSettings()->saveIfNeeded();
}

void PreferencesSettingsTab::setupToolbarLabelsControls() {
    contentHost.addAndMakeVisible(showToolbarLabelsToggle);
    // DEFAULT TRUE: the captions have always shown, so an install that never opens this tab is unaffected.
    showToolbarLabelsToggle.setToggleState(synth::ui::showToolbarLabels(appProperties.getUserSettings()),
                                           juce::dontSendNotification);
    showToolbarLabelsToggle.setTooltip("Show text under the top bar and canvas toolbar buttons");
    showToolbarLabelsToggle.onClick = [this] { persistShowToolbarLabels(showToolbarLabelsToggle.getToggleState()); };
    setupMidiRemoteControls(); // Chained here, the constructor is baselined
}

void PreferencesSettingsTab::layoutToolbarLabelsGroup(
    int& y, int contentWidth, bool previousGroupWasVisible,
    const std::function<bool(std::initializer_list<juce::Component*>)>& groupMatches,
    const std::function<void(std::initializer_list<juce::Component*>, bool)>& setGroupVisible) {
    enterCategory(Category::Panels, y);
    const std::initializer_list<juce::Component*> comps = {&showToolbarLabelsToggle};
    const bool visible = groupMatches(comps);
    setGroupVisible(comps, visible);
    if (visible) {
        if (previousGroupWasVisible) {
            // Same divider math as layoutPanelDetachModeGroup().
            y += 10;
            dividerBounds.push_back(juce::Rectangle<int>{0, y, contentWidth, 1});
            y += 11;
        }
        showToolbarLabelsToggle.setBounds({0, y, contentWidth, 24});
        y += 24;
    }
    layoutMidiRemoteGroup(y, contentWidth, visible || previousGroupWasVisible, groupMatches, setGroupVisible);
}
