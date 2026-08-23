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

// Settings key for the scroll-direction preference. Duplicated rather than shared with
// MainComponent::kNaturalScrollingKey (which is what READS it) for the same reason
// "timelineLoopSelectionArms" is duplicated between here and TimelinePanelComponent: a one-line
// string constant is not worth a header dependency from a settings tab onto MainComponent. DEFAULT
// TRUE — natural is what every scrolling surface in the app already does, so an install that never
// opens this tab is unaffected.
constexpr const char* kNaturalScrollingKey = "naturalScrolling";

// The wheel-ZOOM direction preference, duplicated from MainComponent::kZoomScrollUpZoomsInKey for
// exactly the reason kNaturalScrollingKey above is. DEFAULT TRUE — "up zooms in" is what both
// wheel-zoom surfaces did before this preference existed, so an install that never opens this tab is
// unaffected.
constexpr const char* kZoomScrollUpZoomsInKey = "zoomScrollUpZoomsIn";

// The piano roll's key-label density (PianoRollComponent::KeyLabelMode), read by
// TimelinePanelComponent::reloadPianoRollAppearancePrefs(). "all" is the default and matches the
// roll's own KeyLabelMode default, so an install that never opens this tab is unaffected.
constexpr const char* kPianoRollKeyLabelsKey = "pianoRollKeyLabels";

// Group-separator alpha. Softened from 0.18: at that contrast the hairlines read as table borders
// and boxed each preference in, which is the same complaint that produced the gentler rule under
// the Keyboard Shortcuts tab's section headers (see its kDividerAlpha — keep the two in step).
constexpr float kDividerAlpha = 0.12f;

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

    addAndMakeVisible(alignmentGuideToggle);
    alignmentGuideToggle.setToggleState(appProperties.getUserSettings()->getBoolValue("alignmentGuidesEnabled", true),
                                        juce::dontSendNotification);
    alignmentGuideToggle.setTooltip("Shows snap/alignment guides on the canvas while dragging a module.");
    alignmentGuideToggle.onClick = [this] { persistAlignmentGuidesEnabled(alignmentGuideToggle.getToggleState()); };

    addAndMakeVisible(defaultDualIOToggle);
    defaultDualIOToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue("defaultDualIOForNewModules", false), juce::dontSendNotification);
    defaultDualIOToggle.setTooltip("Splits the audio jacks on every stereo-capable module — FX, Voice Mixer output, "
                                   "Oscillator, Wavetable, Filter, VCA and Sampler. Applies to modules already on the "
                                   "canvas as well as new ones. Card heights do not change.");
    defaultDualIOToggle.onClick = [this] { persistDefaultDualIOForNewModules(defaultDualIOToggle.getToggleState()); };

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

    addAndMakeVisible(naturalScrollingToggle);
    // DEFAULT TRUE: "natural" is the juce::Viewport convention every scrolling surface in the app
    // already follows, so an install that never touches this preference behaves exactly as before.
    naturalScrollingToggle.setToggleState(appProperties.getUserSettings()->getBoolValue(kNaturalScrollingKey, true),
                                          juce::dontSendNotification);
    naturalScrollingToggle.setTooltip("On (the default) scrolls the way the rest of the app and your OS do. Turn it "
                                      "off to invert the wheel and trackpad in the timeline and the piano roll.");
    naturalScrollingToggle.onClick = [this] { persistNaturalScrolling(naturalScrollingToggle.getToggleState()); };

    addAndMakeVisible(naturalScrollingHint);
    naturalScrollingHint.setText("Affects the timeline and the piano roll. The graph canvas pans instead of "
                                 "scrolling and is unaffected.",
                                 juce::dontSendNotification);
    naturalScrollingHint.setFont(juce::Font(juce::FontOptions(11.5f)));
    naturalScrollingHint.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId).withAlpha(0.65f));

    addAndMakeVisible(zoomScrollUpZoomsInToggle);
    // DEFAULT TRUE, and deliberately the same idiom as the row above: "up zooms in" is what both
    // wheel-zoom surfaces already did, so nobody's gesture changes until they ask for it here.
    zoomScrollUpZoomsInToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(kZoomScrollUpZoomsInKey, true), juce::dontSendNotification);
    zoomScrollUpZoomsInToggle.setTooltip("On (the default) zooms IN when you scroll up with Cmd (or Cmd+Shift) held. "
                                         "Turn it off if you expect scrolling up to zoom out.");
    zoomScrollUpZoomsInToggle.onClick = [this] {
        persistZoomScrollUpZoomsIn(zoomScrollUpZoomsInToggle.getToggleState());
    };

    addAndMakeVisible(zoomScrollUpZoomsInHint);
    zoomScrollUpZoomsInHint.setText("Affects Cmd (horizontal) and Cmd+Shift (vertical) wheel-zoom in the timeline and "
                                    "the piano roll. Plain scrolling follows the setting above.",
                                    juce::dontSendNotification);
    zoomScrollUpZoomsInHint.setFont(juce::Font(juce::FontOptions(11.5f)));
    zoomScrollUpZoomsInHint.setColour(juce::Label::textColourId,
                                      findColour(juce::Label::textColourId).withAlpha(0.65f));

    addAndMakeVisible(pianoRollKeyLabelsToggle);
    // DEFAULT TRUE ("all"): matches PianoRollComponent::KeyLabelMode::AllNotes, its own default,
    // so an install that never opens this tab sees no change.
    pianoRollKeyLabelsToggle.setToggleState(
        appProperties.getUserSettings()->getValue(kPianoRollKeyLabelsKey, "all").equalsIgnoreCase("all"),
        juce::dontSendNotification);
    pianoRollKeyLabelsToggle.setTooltip("On labels every key in the piano roll's keys column. Off labels only the Cs.");
    pianoRollKeyLabelsToggle.onClick = [this] {
        persistPianoRollKeyLabelMode(pianoRollKeyLabelsToggle.getToggleState());
    };
}

void PreferencesSettingsTab::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));

    // Group separators. Drawn from the text colour at low alpha rather than a theme token so the
    // rule stays legible on both light and dark themes without needing one of its own.
    g.setColour(findColour(juce::Label::textColourId).withAlpha(kDividerAlpha));
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

    alignmentGuideToggle.setBounds(bounds.removeFromTop(24));
    addDivider();

    defaultDualIOToggle.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(10);
    perModuleDefaultsButton.setBounds(bounds.removeFromTop(24).removeFromLeft(220));
    bounds.removeFromTop(10);

    loopSelectionArmsToggle.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(10);

    timelineFeatureToggle.setBounds(bounds.removeFromTop(24));
    addDivider();

    naturalScrollingToggle.setBounds(bounds.removeFromTop(24));
    // Indented under the toggle it explains, so the hint reads as a caption rather than as another
    // preference row.
    naturalScrollingHint.setBounds(bounds.removeFromTop(18).withTrimmedLeft(24));
    // Same group (no divider): both are wheel-direction preferences, and separating them would imply
    // the zoom one is unrelated to the row it qualifies.
    bounds.removeFromTop(10);
    zoomScrollUpZoomsInToggle.setBounds(bounds.removeFromTop(24));
    zoomScrollUpZoomsInHint.setBounds(bounds.removeFromTop(18).withTrimmedLeft(24));
    addDivider();

    pianoRollKeyLabelsToggle.setBounds(bounds.removeFromTop(24));
}

void PreferencesSettingsTab::setGraphEditor(GraphEditor* ge) {
    graphEditor = ge;
    if (graphEditor == nullptr)
        return;
    graphEditor->setSmartConnectionMode(modeFromComboId(smartConnectionCombo.getSelectedId()));
    graphEditor->setDoubleClickPortDisconnectEnabled(doubleClickDisconnectToggle.getToggleState());
    graphEditor->setAlignmentGuidesEnabled(alignmentGuideToggle.getToggleState());
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

bool PreferencesSettingsTab::isAlignmentGuidesEnabled() const { return alignmentGuideToggle.getToggleState(); }

void PreferencesSettingsTab::setAlignmentGuidesEnabled(bool enabled) {
    alignmentGuideToggle.setToggleState(enabled, juce::dontSendNotification);
    persistAlignmentGuidesEnabled(enabled);
}

bool PreferencesSettingsTab::getDefaultDualIOForNewModules() const { return defaultDualIOToggle.getToggleState(); }

void PreferencesSettingsTab::setDefaultDualIOForNewModules(bool enabled) {
    defaultDualIOToggle.setToggleState(enabled, juce::dontSendNotification);
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
    // preference does not scale, and the "Show timeline" kill switch already needs the one it has.
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

void PreferencesSettingsTab::persistAlignmentGuidesEnabled(bool enabled) {
    appProperties.getUserSettings()->setValue("alignmentGuidesEnabled", enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    if (graphEditor)
        graphEditor->setAlignmentGuidesEnabled(enabled);
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

void PreferencesSettingsTab::persistDefaultDualIOForNewModules(bool enabled) {
    appProperties.getUserSettings()->setValue("defaultDualIOForNewModules", enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    if (graphEditor) {
        graphEditor->setDefaultDualIOForNewModules(enabled);
        // Deliberately changing the preference re-lays what is already on the canvas as well.
        // Scoping it to new modules made it look broken: the obvious way to test a setting is to
        // flip it and look at the patch in front of you, which never changed. Only this path
        // retro-applies — setGraphEditor() and the startup restore must not touch the patch.
        graphEditor->applyDualIOToExistingModules(enabled);
    }
}
