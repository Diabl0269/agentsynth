#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"

// Concern: timeline/piano-roll editing preferences — loop-selection-arms, double-click-spans-
// locators, natural scrolling, wheel-zoom direction, and piano-roll key-label density.

bool PreferencesSettingsTab::isLoopSelectionArmsEnabled() const { return loopSelectionArmsToggle.getToggleState(); }

void PreferencesSettingsTab::setLoopSelectionArmsEnabled(bool enabled) {
    loopSelectionArmsToggle.setToggleState(enabled, juce::dontSendNotification);
    persistLoopSelectionArms(enabled);
}

bool PreferencesSettingsTab::isDoubleClickSpansLocatorsEnabled() const {
    return doubleClickSpansLocatorsToggle.getToggleState();
}

void PreferencesSettingsTab::setDoubleClickSpansLocatorsEnabled(bool enabled) {
    doubleClickSpansLocatorsToggle.setToggleState(enabled, juce::dontSendNotification);
    persistDoubleClickSpansLocators(enabled);
}

void PreferencesSettingsTab::persistDoubleClickSpansLocators(bool enabled) {
    // Nothing live to push: TimelineClipLaneArea reads this key at use time (on the next
    // double-click), the same way the row above it is read by the timeline's P handler.
    appProperties.getUserSettings()->setValue(kTimelineDoubleClickSpansLocatorsKey, enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
}

void PreferencesSettingsTab::persistLoopSelectionArms(bool enabled) {
    // Read at use time by TimelinePanelComponent's P handler and MainComponent's
    // onLoopRangeRequested — nothing live to push here, unlike the GraphEditor settings above.
    appProperties.getUserSettings()->setValue("timelineLoopSelectionArms", enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
}

bool PreferencesSettingsTab::isNaturalScrollingEnabled() const { return naturalScrollingToggle.getToggleState(); }

void PreferencesSettingsTab::setNaturalScrollingEnabled(bool enabled) {
    naturalScrollingToggle.setToggleState(enabled, juce::dontSendNotification);
    persistNaturalScrolling(enabled);
}

void PreferencesSettingsTab::persistNaturalScrolling(bool enabled) {
    appProperties.getUserSettings()->setValue(kNaturalScrollingKey, enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    // No live push from here, unlike the GraphEditor settings below: juce::PropertiesFile is a
    // ChangeBroadcaster, and MainComponent listens to it precisely so a scroll-direction change
    // reaches the timeline and the piano roll without this tab having to know they exist (see
    // MainComponent::applyNaturalScrollingPreference). That is also why there is no
    // onNaturalScrollingToggled callback for SettingsWindow to wire — one constructor argument per
    // preference does not scale.
}

bool PreferencesSettingsTab::isZoomScrollUpZoomsInEnabled() const { return zoomScrollUpZoomsInToggle.getToggleState(); }

void PreferencesSettingsTab::setZoomScrollUpZoomsInEnabled(bool enabled) {
    zoomScrollUpZoomsInToggle.setToggleState(enabled, juce::dontSendNotification);
    persistZoomScrollUpZoomsIn(enabled);
}

void PreferencesSettingsTab::persistZoomScrollUpZoomsIn(bool enabled) {
    appProperties.getUserSettings()->setValue(kZoomScrollUpZoomsInKey, enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    // No live push from here either: the SAME juce::PropertiesFile ChangeBroadcaster path
    // persistNaturalScrolling documents above carries it, landing in
    // MainComponent::applyZoomScrollPreference.
}

bool PreferencesSettingsTab::isPianoRollKeyLabelModeAll() const { return pianoRollKeyLabelsToggle.getToggleState(); }

void PreferencesSettingsTab::setPianoRollKeyLabelModeAll(bool labelEveryKey) {
    pianoRollKeyLabelsToggle.setToggleState(labelEveryKey, juce::dontSendNotification);
    persistPianoRollKeyLabelMode(labelEveryKey);
}

void PreferencesSettingsTab::persistPianoRollKeyLabelMode(bool labelEveryKey) {
    appProperties.getUserSettings()->setValue(kPianoRollKeyLabelsKey, labelEveryKey ? "all" : "c");
    appProperties.getUserSettings()->saveIfNeeded();
    // No live push from here: TimelinePanelComponent::reloadPianoRollAppearancePrefs() reads this
    // key directly, the same "no onXToggled callback" reasoning persistNaturalScrolling documents
    // — MainComponent wires the live re-push in a parallel task.
}

void PreferencesSettingsTab::styleMutedHintLabel(juce::Label& hint) {
    hint.setFont(juce::Font(juce::FontOptions(11.5f)));
    hint.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.65f));
    // Wrap instead of squeeze: the default minimum-horizontal-scale (~0.7) lets drawFittedText
    // cram an over-wide single line into the box by shrinking it horizontally, which is exactly
    // the "cramped/narrow" look this was fixed for. With kHintHeight giving room for two lines,
    // there is never a reason to squeeze instead of wrapping.
    hint.setMinimumHorizontalScale(1.0f);
    hint.setJustificationType(juce::Justification::topLeft);
}
