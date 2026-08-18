#pragma once

#include "GraphEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// The Settings "Preferences" tab (issues #216 / #217).
//
// Holds editor behaviour that is not appearance: smart-connection mode and double-click
// port disconnect. Each control persists through juce::ApplicationProperties and, when a
// GraphEditor is wired, pushes live so the canvas does not wait for a restart.
//
// NOTE: PreferencesSettingsTab.cpp MUST be added to BOTH the app target AND the test
// target in CMakeLists.txt.
class PreferencesSettingsTab : public juce::Component {
public:
    explicit PreferencesSettingsTab(juce::ApplicationProperties& props);
    ~PreferencesSettingsTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Called by SettingsWindow once the tab exists; pushes the persisted values onto the canvas.
    void setGraphEditor(GraphEditor* ge);

    // Testing hooks ---------------------------------------------------------
    GraphEditor::SmartConnectionMode getSmartConnectionMode() const;
    void setSmartConnectionMode(GraphEditor::SmartConnectionMode mode);
    bool isDoubleClickPortDisconnectEnabled() const;
    void setDoubleClickPortDisconnectEnabled(bool enabled);
    bool getDefaultDualIOForNewModules() const;
    void setDefaultDualIOForNewModules(bool enabled);

private:
    void persistSmartConnectionMode(GraphEditor::SmartConnectionMode mode);
    void persistDoubleClickPortDisconnect(bool enabled);
    void persistDefaultDualIOForNewModules(bool enabled);

    juce::ApplicationProperties& appProperties;
    GraphEditor* graphEditor{nullptr}; // weak, owned by MainComponent

    juce::Label titleLabel;
    juce::Label smartConnectionLabel;
    juce::ComboBox smartConnectionCombo;
    juce::ToggleButton doubleClickDisconnectToggle{"Double-click port to disconnect"};
    juce::Label defaultDualIOLabel;
    juce::ComboBox defaultDualIOCombo;
    juce::TextButton perModuleDefaultsButton{"Per-module I/O defaults..."};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesSettingsTab)
};
