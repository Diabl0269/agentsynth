#include "PreferencesSettingsTab.h"

namespace {
int comboIdFromMode(GraphEditor::SmartConnectionMode mode) {
    switch (mode) {
    case GraphEditor::SmartConnectionMode::Off:
        return 1;
    case GraphEditor::SmartConnectionMode::NewOnly:
        return 2;
    case GraphEditor::SmartConnectionMode::AllMoves:
        return 4;
    case GraphEditor::SmartConnectionMode::NewAndUnwired:
    default:
        return 3;
    }
}

GraphEditor::SmartConnectionMode modeFromComboId(int id) {
    switch (id) {
    case 1:
        return GraphEditor::SmartConnectionMode::Off;
    case 2:
        return GraphEditor::SmartConnectionMode::NewOnly;
    case 4:
        return GraphEditor::SmartConnectionMode::AllMoves;
    default:
        return GraphEditor::SmartConnectionMode::NewAndUnwired;
    }
}

int comboIdFromDualIODefault(bool enabled) { return enabled ? 2 : 1; }

bool dualIODefaultFromComboId(int id) { return id == 2; }
} // namespace

PreferencesSettingsTab::PreferencesSettingsTab(juce::ApplicationProperties& props)
    : appProperties(props) {
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Preferences", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));

    addAndMakeVisible(smartConnectionLabel);
    smartConnectionLabel.setText("Smart connections:", juce::dontSendNotification);
    smartConnectionLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    addAndMakeVisible(smartConnectionCombo);
    smartConnectionCombo.addItem("Off", 1);
    smartConnectionCombo.addItem("New modules only", 2);
    smartConnectionCombo.addItem("When main I/O is free", 3);
    smartConnectionCombo.addItem("All module moves", 4);
    smartConnectionCombo.setTooltip("Suggest cables to nearby modules while placing or moving a card.");
    {
        const auto mode = GraphEditor::smartConnectionModeFromString(
            appProperties.getUserSettings()->getValue("smartConnectionMode", "NewAndUnwired"));
        smartConnectionCombo.setSelectedId(comboIdFromMode(mode), juce::dontSendNotification);
    }
    smartConnectionCombo.onChange = [this] {
        persistSmartConnectionMode(modeFromComboId(smartConnectionCombo.getSelectedId()));
    };

    addAndMakeVisible(doubleClickDisconnectToggle);
    doubleClickDisconnectToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue("doubleClickPortDisconnect", true), juce::dontSendNotification);
    doubleClickDisconnectToggle.setTooltip(
        "When on, double-clicking a connected jack removes every cable on that port.");
    doubleClickDisconnectToggle.onClick = [this] {
        persistDoubleClickPortDisconnect(doubleClickDisconnectToggle.getToggleState());
    };

    addAndMakeVisible(defaultDualIOLabel);
    defaultDualIOLabel.setText("Default FX I/O jack layout:", juce::dontSendNotification);
    defaultDualIOLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    addAndMakeVisible(defaultDualIOCombo);
    defaultDualIOCombo.addItem("Single Audio jack", 1);
    defaultDualIOCombo.addItem("Split Left/Right jacks", 2);
    defaultDualIOCombo.setTooltip("Applies to newly created modules that support Dual I/O (FX + Voice Mixer output).");
    defaultDualIOCombo.setSelectedId(
        comboIdFromDualIODefault(appProperties.getUserSettings()->getBoolValue("defaultDualIOForNewModules", false)),
        juce::dontSendNotification);
    defaultDualIOCombo.onChange = [this] {
        persistDefaultDualIOForNewModules(dualIODefaultFromComboId(defaultDualIOCombo.getSelectedId()));
    };

    addAndMakeVisible(perModuleDefaultsButton);
    perModuleDefaultsButton.setEnabled(false);
    perModuleDefaultsButton.setTooltip("Per-module I/O default overrides are planned in a follow-up preference.");

    addAndMakeVisible(loopSelectionArmsToggle);
    loopSelectionArmsToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue("timelineLoopSelectionArms", true), juce::dontSendNotification);
    loopSelectionArmsToggle.setTooltip(
        "When on, pressing P in the timeline both places the loop locators around the selection AND switches "
        "looping on. When off, P only places the locators (use L to toggle looping).");
    loopSelectionArmsToggle.onClick = [this] { persistLoopSelectionArms(loopSelectionArmsToggle.getToggleState()); };

    addAndMakeVisible(timelineFeatureToggle);
    // DEFAULT TRUE: existing users already have the timeline visible/hidden per their own
    // "timelinePanelVisible" choice — this is a higher-level kill switch on top of that, and must
    // not itself change behaviour for anyone who has never touched it.
    timelineFeatureToggle.setToggleState(appProperties.getUserSettings()->getBoolValue("timelineFeatureEnabled", true),
                                         juce::dontSendNotification);
    timelineFeatureToggle.setTooltip(
        "When off, the timeline panel, its toolbar button, and Cmd+T / Space are hidden — the "
        "timeline document and audio-engine playback are untouched, so turning this back on "
        "restores exactly where you left off.");
    timelineFeatureToggle.onClick = [this] { persistTimelineFeatureEnabled(timelineFeatureToggle.getToggleState()); };
}

void PreferencesSettingsTab::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void PreferencesSettingsTab::resized() {
    auto bounds = getLocalBounds().reduced(12);

    titleLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(12);

    auto smartRow = bounds.removeFromTop(24);
    smartConnectionLabel.setBounds(smartRow.removeFromLeft(160));
    smartConnectionCombo.setBounds(smartRow.removeFromLeft(220));
    bounds.removeFromTop(10);

    doubleClickDisconnectToggle.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(10);

    auto dualIORow = bounds.removeFromTop(24);
    defaultDualIOLabel.setBounds(dualIORow.removeFromLeft(220));
    defaultDualIOCombo.setBounds(dualIORow.removeFromLeft(220));
    bounds.removeFromTop(10);

    perModuleDefaultsButton.setBounds(bounds.removeFromTop(24).removeFromLeft(220));
    bounds.removeFromTop(10);

    loopSelectionArmsToggle.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(10);

    timelineFeatureToggle.setBounds(bounds.removeFromTop(24));
}

void PreferencesSettingsTab::setGraphEditor(GraphEditor* ge) {
    graphEditor = ge;
    if (graphEditor == nullptr)
        return;
    graphEditor->setSmartConnectionMode(modeFromComboId(smartConnectionCombo.getSelectedId()));
    graphEditor->setDoubleClickPortDisconnectEnabled(doubleClickDisconnectToggle.getToggleState());
    graphEditor->setDefaultDualIOForNewModules(dualIODefaultFromComboId(defaultDualIOCombo.getSelectedId()));
}

GraphEditor::SmartConnectionMode PreferencesSettingsTab::getSmartConnectionMode() const {
    return modeFromComboId(smartConnectionCombo.getSelectedId());
}

void PreferencesSettingsTab::setSmartConnectionMode(GraphEditor::SmartConnectionMode mode) {
    smartConnectionCombo.setSelectedId(comboIdFromMode(mode), juce::dontSendNotification);
    persistSmartConnectionMode(mode);
}

bool PreferencesSettingsTab::isDoubleClickPortDisconnectEnabled() const {
    return doubleClickDisconnectToggle.getToggleState();
}

void PreferencesSettingsTab::setDoubleClickPortDisconnectEnabled(bool enabled) {
    doubleClickDisconnectToggle.setToggleState(enabled, juce::dontSendNotification);
    persistDoubleClickPortDisconnect(enabled);
}

bool PreferencesSettingsTab::getDefaultDualIOForNewModules() const {
    return dualIODefaultFromComboId(defaultDualIOCombo.getSelectedId());
}

void PreferencesSettingsTab::setDefaultDualIOForNewModules(bool enabled) {
    defaultDualIOCombo.setSelectedId(comboIdFromDualIODefault(enabled), juce::dontSendNotification);
    persistDefaultDualIOForNewModules(enabled);
}

bool PreferencesSettingsTab::isLoopSelectionArmsEnabled() const { return loopSelectionArmsToggle.getToggleState(); }

void PreferencesSettingsTab::setLoopSelectionArmsEnabled(bool enabled) {
    loopSelectionArmsToggle.setToggleState(enabled, juce::dontSendNotification);
    persistLoopSelectionArms(enabled);
}

void PreferencesSettingsTab::persistLoopSelectionArms(bool enabled) {
    // Read at use time by TimelinePanelComponent's P handler and MainComponent's
    // onLoopRangeRequested — nothing live to push here, unlike the GraphEditor settings above.
    appProperties.getUserSettings()->setValue("timelineLoopSelectionArms", enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
}

bool PreferencesSettingsTab::isTimelineFeatureEnabled() const { return timelineFeatureToggle.getToggleState(); }

void PreferencesSettingsTab::setTimelineFeatureEnabled(bool enabled) {
    timelineFeatureToggle.setToggleState(enabled, juce::dontSendNotification);
    persistTimelineFeatureEnabled(enabled);
}

void PreferencesSettingsTab::persistTimelineFeatureEnabled(bool enabled) {
    appProperties.getUserSettings()->setValue("timelineFeatureEnabled", enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    if (onTimelineFeatureToggled)
        onTimelineFeatureToggled(enabled);
}

void PreferencesSettingsTab::persistSmartConnectionMode(GraphEditor::SmartConnectionMode mode) {
    appProperties.getUserSettings()->setValue("smartConnectionMode", GraphEditor::smartConnectionModeToString(mode));
    appProperties.getUserSettings()->saveIfNeeded();
    if (graphEditor)
        graphEditor->setSmartConnectionMode(mode);
}

void PreferencesSettingsTab::persistDoubleClickPortDisconnect(bool enabled) {
    appProperties.getUserSettings()->setValue("doubleClickPortDisconnect", enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    if (graphEditor)
        graphEditor->setDoubleClickPortDisconnectEnabled(enabled);
}

void PreferencesSettingsTab::persistDefaultDualIOForNewModules(bool enabled) {
    appProperties.getUserSettings()->setValue("defaultDualIOForNewModules", enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    if (graphEditor)
        graphEditor->setDefaultDualIOForNewModules(enabled);
}
