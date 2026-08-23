#pragma once

#include "GraphEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

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
    bool isAlignmentGuidesEnabled() const;
    void setAlignmentGuidesEnabled(bool enabled);
    bool getDefaultDualIOForNewModules() const;
    void setDefaultDualIOForNewModules(bool enabled);
    bool isLoopSelectionArmsEnabled() const;
    void setLoopSelectionArmsEnabled(bool enabled);
    bool isTimelineFeatureEnabled() const;
    void setTimelineFeatureEnabled(bool enabled);
    bool isNaturalScrollingEnabled() const;
    void setNaturalScrollingEnabled(bool enabled);
    bool isZoomScrollUpZoomsInEnabled() const;
    void setZoomScrollUpZoomsInEnabled(bool enabled);
    // "all" (every key labelled) vs "c" (only the Cs) — PianoRollComponent::KeyLabelMode, read by
    // TimelinePanelComponent::reloadPianoRollAppearancePrefs(). true == "all" (the default).
    bool isPianoRollKeyLabelModeAll() const;
    void setPianoRollKeyLabelModeAll(bool labelEveryKey);

    // Fired from persistTimelineFeatureEnabled() after the value is saved, so a live MainComponent
    // can hide/show the timeline entry points without waiting for a restart. Null in every context
    // that doesn't wire it (e.g. a headless test that only checks persistence).
    std::function<void(bool)> onTimelineFeatureToggled;

private:
    void persistSmartConnectionMode(GraphEditor::SmartConnectionMode mode);
    void persistDoubleClickPortDisconnect(bool enabled);
    void persistAlignmentGuidesEnabled(bool enabled);
    void persistDefaultDualIOForNewModules(bool enabled);
    void persistLoopSelectionArms(bool enabled);
    void persistTimelineFeatureEnabled(bool enabled);
    void persistNaturalScrolling(bool enabled);
    void persistZoomScrollUpZoomsIn(bool enabled);
    void persistPianoRollKeyLabelMode(bool labelEveryKey);

    juce::ApplicationProperties& appProperties;
    GraphEditor* graphEditor{nullptr}; // weak, owned by MainComponent

    juce::Label titleLabel;
    juce::Label smartConnectionLabel;
    juce::ComboBox smartConnectionCombo;
    juce::ToggleButton doubleClickDisconnectToggle{"Double-click port to disconnect"};
    // Moved here from AppearanceSettingsTab: this is canvas-editing behaviour (whether the graph
    // shows snap guides while dragging), the same family as the two toggles above it, not an
    // appearance/theme setting. Persistence key ("alignmentGuidesEnabled") is unchanged.
    juce::ToggleButton alignmentGuideToggle{"Show Alignment Guides"};
    // One line, one control: the old label + two-item ComboBox said the same thing in two widgets
    // and read as a mode picker rather than the on/off it actually is.
    juce::ToggleButton defaultDualIOToggle{"Split Left/Right jacks on new modules"};
    juce::TextButton perModuleDefaultsButton{"Per-module I/O defaults..."};
    juce::ToggleButton loopSelectionArmsToggle{"Timeline: P (loop selection) also switches looping on"};
    juce::ToggleButton timelineFeatureToggle{"Show timeline (experimental)"};
    juce::ToggleButton naturalScrollingToggle{"Natural scrolling"};
    // The one preference whose label needs a second line to explain WHICH surfaces it touches — a
    // bare "Natural scrolling" toggle in an app that also has a pannable canvas would read as
    // applying to everything.
    juce::Label naturalScrollingHint;
    // Sits directly under the natural-scrolling pair because it is the same gesture with a modifier
    // held, and users reach for both in the same visit. Independent of it, though: this one governs
    // the Cmd / Cmd+Shift wheel-ZOOM branches only, which is what its own caption spells out.
    juce::ToggleButton zoomScrollUpZoomsInToggle{"Scroll up zooms in"};
    juce::Label zoomScrollUpZoomsInHint;
    // On (the default) labels every row in the piano roll's keys column; off labels only the Cs —
    // PianoRollComponent::KeyLabelMode::AllNotes / OctavesOnly.
    juce::ToggleButton pianoRollKeyLabelsToggle{"Label every key"};

    // Hairline rules between preference groups, painted in paint() from these bounds.
    std::vector<juce::Rectangle<int>> dividerBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesSettingsTab)
};
