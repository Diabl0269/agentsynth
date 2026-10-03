#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"
#include "UI/Layout/AppTooltipWindow.h"

// Concern: the "Show info tooltips" row (docs/layout/animation.md#tooltips; Panels & Windows). Off hides the tooltips
// that explain a control; helper tips keep showing. Nothing is pushed from here: every synth::ui::AppTooltipWindow
// reads synth::ui::kShowInfoTooltipsKey from the user settings each time it is asked for a tip, so a toggle applies at
// once, in the main window and every detached window. Chained between the panel-detach-mode row and the MIDI Remote
// group.

bool PreferencesSettingsTab::isShowInfoTooltipsEnabled() const { return showInfoTooltipsToggle.getToggleState(); }

void PreferencesSettingsTab::setShowInfoTooltipsEnabled(bool enabled) {
    showInfoTooltipsToggle.setToggleState(enabled, juce::dontSendNotification);
    persistShowInfoTooltips(enabled);
}

void PreferencesSettingsTab::persistShowInfoTooltips(bool enabled) {
    appProperties.getUserSettings()->setValue(synth::ui::kShowInfoTooltipsKey, enabled);
    appProperties.getUserSettings()->saveIfNeeded();
}

void PreferencesSettingsTab::setupInfoTooltipsControls() {
    contentHost.addAndMakeVisible(showInfoTooltipsToggle);
    // DEFAULT TRUE: tooltips have always shown, so an install that never opens this tab is unaffected.
    showInfoTooltipsToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(synth::ui::kShowInfoTooltipsKey, true),
        juce::dontSendNotification);
    showInfoTooltipsToggle.setTooltip("Turn off to stop the tooltips that explain a control when you hover it. Tips "
                                      "that tell you something the screen does not show, such as what plays into a "
                                      "mixer channel, keep appearing.");
    // This toggle's own tooltip is a helper tip: with info tooltips off it must still say how to bring them back.
    synth::ui::markHelperTooltip(showInfoTooltipsToggle);
    showInfoTooltipsToggle.onClick = [this] { persistShowInfoTooltips(showInfoTooltipsToggle.getToggleState()); };

    contentHost.addAndMakeVisible(showInfoTooltipsHint);
    showInfoTooltipsHint.setText(
        "Hover tips that explain a control fade in after a moment. Helper tips, like who plays "
        "into a mixer channel, are not affected.",
        juce::dontSendNotification);
    styleMutedHintLabel(showInfoTooltipsHint);
    setupMidiRemoteControls(); // Chained here, the constructor is baselined
}

void PreferencesSettingsTab::layoutInfoTooltipsGroup(
    int& y, int contentWidth, bool previousGroupWasVisible,
    const std::function<bool(std::initializer_list<juce::Component*>)>& groupMatches,
    const std::function<void(std::initializer_list<juce::Component*>, bool)>& setGroupVisible) {
    enterCategory(Category::Panels, y);
    const std::initializer_list<juce::Component*> comps = {&showInfoTooltipsToggle, &showInfoTooltipsHint};
    const bool visible = groupMatches(comps);
    setGroupVisible(comps, visible);
    if (visible) {
        if (previousGroupWasVisible) {
            // Same divider math as layoutPanelDetachModeGroup().
            y += 10;
            dividerBounds.push_back(juce::Rectangle<int>{0, y, contentWidth, 1});
            y += 11;
        }
        showInfoTooltipsToggle.setBounds({0, y, contentWidth, 24});
        y += 24;
        showInfoTooltipsHint.setBounds({24, y, contentWidth - 24, kHintHeight});
        y += kHintHeight;
    }
    layoutMidiRemoteGroup(y, contentWidth, visible || previousGroupWasVisible, groupMatches, setGroupVisible);
}
