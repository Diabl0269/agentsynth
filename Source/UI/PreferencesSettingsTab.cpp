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

    addAndMakeVisible(defaultDualIOToggle);
    defaultDualIOToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue("defaultDualIOForNewModules", false), juce::dontSendNotification);
    defaultDualIOToggle.setTooltip("Applies to newly created modules with a Dual I/O toggle — FX, Voice Mixer output, "
                                   "and the voice modules (Oscillator, Wavetable, Filter, VCA, Sampler). Off gives "
                                   "them a single Audio jack; existing modules on the canvas are not changed.");
    defaultDualIOToggle.onClick = [this] { persistDefaultDualIOForNewModules(defaultDualIOToggle.getToggleState()); };

    addAndMakeVisible(perModuleDefaultsButton);
    perModuleDefaultsButton.setEnabled(false);
    perModuleDefaultsButton.setTooltip("Per-module I/O default overrides are planned in a follow-up preference.");
}

void PreferencesSettingsTab::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));

    // Group separators. Drawn from the text colour at low alpha rather than a theme token so the
    // rule stays legible on both light and dark themes without needing one of its own.
    g.setColour(findColour(juce::Label::textColourId).withAlpha(0.18f));
    for (const auto& divider : dividerBounds)
        g.fillRect(divider);
}

void PreferencesSettingsTab::resized() {
    auto bounds = getLocalBounds().reduced(12);
    dividerBounds.clear();

    // Each group is followed by a hairline rule, so related settings read as one block instead of
    // an undifferentiated stack of rows.
    auto addDivider = [this, &bounds] {
        bounds.removeFromTop(10);
        dividerBounds.push_back(bounds.removeFromTop(1));
        bounds.removeFromTop(10);
    };

    titleLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(12);

    auto smartRow = bounds.removeFromTop(24);
    smartConnectionLabel.setBounds(smartRow.removeFromLeft(160));
    smartConnectionCombo.setBounds(smartRow.removeFromLeft(220));
    addDivider();

    doubleClickDisconnectToggle.setBounds(bounds.removeFromTop(24));
    addDivider();

    defaultDualIOToggle.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(10);
    perModuleDefaultsButton.setBounds(bounds.removeFromTop(24).removeFromLeft(220));
}

void PreferencesSettingsTab::setGraphEditor(GraphEditor* ge) {
    graphEditor = ge;
    if (graphEditor == nullptr)
        return;
    graphEditor->setSmartConnectionMode(modeFromComboId(smartConnectionCombo.getSelectedId()));
    graphEditor->setDoubleClickPortDisconnectEnabled(doubleClickDisconnectToggle.getToggleState());
    graphEditor->setDefaultDualIOForNewModules(defaultDualIOToggle.getToggleState());
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

bool PreferencesSettingsTab::getDefaultDualIOForNewModules() const { return defaultDualIOToggle.getToggleState(); }

void PreferencesSettingsTab::setDefaultDualIOForNewModules(bool enabled) {
    defaultDualIOToggle.setToggleState(enabled, juce::dontSendNotification);
    persistDefaultDualIOForNewModules(enabled);
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
