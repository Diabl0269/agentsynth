#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/SearchMatch.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>

// Concern: construction, layout, search filter, and the smart-connection-mode / macro-
// auto-port-preference combo-id helpers shared with the getter/setter units.

namespace {
// Group-separator alpha. Softened from 0.18: at that contrast the hairlines read as table borders
// and boxed each preference in, which is the same complaint that produced the gentler rule under
// the Keyboard Shortcuts tab's section headers (see its kDividerAlpha — keep the two in step).
constexpr float kDividerAlpha = 0.12f;

} // namespace

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

int comboIdFromMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference pref) {
    switch (pref) {
    case GraphEditor::MacroAutoPortPreference::AutoCreatePorts:
        return 2;
    case GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs:
        return 3;
    case GraphEditor::MacroAutoPortPreference::Unset:
    default:
        return 1;
    }
}

GraphEditor::MacroAutoPortPreference macroAutoPortPreferenceFromComboId(int id) {
    switch (id) {
    case 2:
        return GraphEditor::MacroAutoPortPreference::AutoCreatePorts;
    case 3:
        return GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs;
    case 1:
    default:
        return GraphEditor::MacroAutoPortPreference::Unset;
    }
}

GraphEditor::MacroAutoPortPreference macroAutoPortPreferenceFromString(const juce::String& s) {
    if (s == "auto")
        return GraphEditor::MacroAutoPortPreference::AutoCreatePorts;
    if (s == "leave")
        return GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs;
    return GraphEditor::MacroAutoPortPreference::Unset;
}

int comboIdFromSideBySide(SmartConnectionEngine::SideBySideMode mode) {
    return mode == SmartConnectionEngine::SideBySideMode::OnlyWithCtrl ? 2 : 1;
}

SmartConnectionEngine::SideBySideMode sideBySideFromComboId(int id) {
    return id == 2 ? SmartConnectionEngine::SideBySideMode::OnlyWithCtrl
                   : SmartConnectionEngine::SideBySideMode::Always;
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

PreferencesSettingsTab::PreferencesSettingsTab(juce::ApplicationProperties& props)
    : appProperties(props) {
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Preferences", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));

    // Live filter: styled like ModuleLibraryComponent's own search box, which
    // is the closest precedent for a live text filter in this app.
    addAndMakeVisible(searchField);
    searchField.setMultiLine(false);
    searchField.setReturnKeyStartsNewLine(false);
    searchField.setEscapeAndReturnKeysConsumed(true);
    searchField.setSelectAllWhenFocused(true);
    searchField.setJustification(juce::Justification::centredLeft);
    searchField.setIndents(6, 0);
    searchField.setFont(juce::Font(juce::FontOptions(13.0f)));
    searchField.setTextToShowWhenEmpty("Filter preferences...", findColour(juce::Label::textColourId).withAlpha(0.5f));
    searchField.setTitle("Filter preferences");
    searchField.setTooltip("Filters the rows below by name or description as you type.");
    searchField.onTextChange = [this] { applySearchFilter(searchField.getText()); };
    searchField.onEscapeKey = [this] {
        if (searchField.getText().isNotEmpty()) {
            // dontSendNotification + a direct applySearchFilter() call, mirroring
            // ModuleLibraryComponent::setSearchText: deterministic regardless of whether
            // juce::TextEditor's own change notification happens to fire synchronously.
            searchField.setText({}, juce::dontSendNotification);
            applySearchFilter({});
        }
    };
    // The preference groups live inside a scroll view's content host, exactly as the Keyboard
    // Shortcuts tab keeps its rows: the title and the search field stay pinned above the scroll
    // region, but the groups scroll when they outgrow the window instead of getting clipped.
    // contentHost's own size is computed in layoutContent; the viewport gives it a vertical scrollbar.
    addAndMakeVisible(contentViewport);
    contentViewport.setViewedComponent(&contentHost, false);
    contentViewport.setScrollBarsShown(true, false);
    contentViewport.setWantsKeyboardFocus(false);

    setupSmartConnectionControls();

    contentHost.addAndMakeVisible(doubleClickDisconnectToggle);
    doubleClickDisconnectToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue("doubleClickPortDisconnect", true), juce::dontSendNotification);
    doubleClickDisconnectToggle.setTooltip(
        "When on, double-clicking a connected jack removes every cable on that port.");
    doubleClickDisconnectToggle.onClick = [this] {
        persistDoubleClickPortDisconnect(doubleClickDisconnectToggle.getToggleState());
    };

    contentHost.addAndMakeVisible(alignmentGuideToggle);
    alignmentGuideToggle.setToggleState(appProperties.getUserSettings()->getBoolValue("alignmentGuidesEnabled", true),
                                        juce::dontSendNotification);
    alignmentGuideToggle.setTooltip("Shows snap/alignment guides on the canvas while dragging a module.");
    alignmentGuideToggle.onClick = [this] { persistAlignmentGuidesEnabled(alignmentGuideToggle.getToggleState()); };

    contentHost.addAndMakeVisible(defaultDualIOToggle);
    defaultDualIOToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue("defaultDualIOForNewModules", false), juce::dontSendNotification);
    defaultDualIOToggle.setTooltip("Splits the audio jacks on every stereo-capable module - FX, Voice Mixer output, "
                                   "Oscillator, Wavetable, Filter, VCA and Sampler. Applies to modules already on the "
                                   "canvas as well as new ones. Card heights do not change.");
    defaultDualIOToggle.onClick = [this] { persistDefaultDualIOForNewModules(defaultDualIOToggle.getToggleState()); };

    dualIOPerModuleOverrides = loadDualIOPerModuleOverrides(appProperties);

    contentHost.addAndMakeVisible(perModuleDefaultsButton);
    perModuleDefaultsButton.setTooltip(
        "Per-module overrides of the Split Left/Right default above - Follow global, Always on, or Always off for "
        "each module type. Applies to modules created after the change, same as the toggle.");
    perModuleDefaultsButton.onClick = [this] {
        auto popup = buildDualIOPerModuleDefaultsPopup();
        juce::CallOutBox::launchAsynchronously(std::move(popup), perModuleDefaultsButton.getScreenBounds(), nullptr);
    };

    contentHost.addAndMakeVisible(macroAutoPortLabel_);
    macroAutoPortLabel_.setText("Macro auto-ports:", juce::dontSendNotification);
    macroAutoPortLabel_.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(macroAutoPortCombo_);
    macroAutoPortCombo_.setTitle("Macro auto-ports");
    macroAutoPortCombo_.addItem("Always ask", 1);
    macroAutoPortCombo_.addItem("Auto-create ports", 2);
    macroAutoPortCombo_.addItem("Leave cables as is", 3);
    macroAutoPortCombo_.setTooltip(
        "When grouping modules that have a cable crossing the new macro's boundary: ask every time "
        "(the default), always create a matching port for it, or always leave the cable exactly as "
        "it is.");
    {
        const auto pref = macroAutoPortPreferenceFromString(
            appProperties.getUserSettings()->getValue(kMacroAutoPortPreferenceKey, "ask"));
        macroAutoPortCombo_.setSelectedId(comboIdFromMacroAutoPortPreference(pref), juce::dontSendNotification);
    }
    macroAutoPortCombo_.onChange = [this] {
        persistMacroAutoPortPreference(macroAutoPortPreferenceFromComboId(macroAutoPortCombo_.getSelectedId()));
    };

    initMacroToggles();

    // Auto-create a mixer channel when a MIDI cable from a Track In node connects to an instrument/macro whose
    // audio reaches the output with no channel yet. Same "plain on/off, ON by default" shape as the two macro
    // auto-port toggles above (see docs/mixer/mixer.md#channels-follow-audio-not-tracks "main workflow").
    contentHost.addAndMakeVisible(mixerAutoCreateChannelOnConnectToggle);
    mixerAutoCreateChannelOnConnectToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue("mixerAutoCreateChannelOnConnect", true),
        juce::dontSendNotification);
    mixerAutoCreateChannelOnConnectToggle.setTooltip(
        "When on (the default), connecting a MIDI track to an instrument or macro whose audio "
        "reaches the output with no mixer channel yet automatically builds one there, in the same "
        "undo step as the connection. When off, wiring stays exactly as it is today - no channel "
        "appears until you ask for one.");
    mixerAutoCreateChannelOnConnectToggle.onClick = [this] {
        persistMixerAutoCreateChannelOnConnect(mixerAutoCreateChannelOnConnectToggle.getToggleState());
    };

    contentHost.addAndMakeVisible(loopSelectionArmsToggle);
    loopSelectionArmsToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue("timelineLoopSelectionArms", true), juce::dontSendNotification);
    loopSelectionArmsToggle.setTooltip(
        "When on, pressing P in the timeline both places the loop locators around the selection AND switches "
        "looping on. When off, P only places the locators (use L to toggle looping).");
    loopSelectionArmsToggle.onClick = [this] { persistLoopSelectionArms(loopSelectionArmsToggle.getToggleState()); };

    initTimelineEditingToggles();

    contentHost.addAndMakeVisible(naturalScrollingToggle);
    // DEFAULT TRUE: "natural" is the juce::Viewport convention every scrolling surface in the app
    // already follows, so an install that never touches this preference behaves exactly as before.
    naturalScrollingToggle.setToggleState(appProperties.getUserSettings()->getBoolValue(kNaturalScrollingKey, true),
                                          juce::dontSendNotification);
    naturalScrollingToggle.setTooltip("On (the default) scrolls the way the rest of the app and your OS do. Turn it "
                                      "off to invert the wheel and trackpad in the timeline and the piano roll.");
    naturalScrollingToggle.onClick = [this] { persistNaturalScrolling(naturalScrollingToggle.getToggleState()); };

    contentHost.addAndMakeVisible(naturalScrollingHint);
    naturalScrollingHint.setText("Affects the timeline and the piano roll. The graph canvas pans instead of "
                                 "scrolling and is unaffected.",
                                 juce::dontSendNotification);
    styleMutedHintLabel(naturalScrollingHint);

    contentHost.addAndMakeVisible(zoomScrollUpZoomsInToggle);
    // DEFAULT TRUE, and deliberately the same idiom as the row above: "up zooms in" is what both
    // wheel-zoom surfaces already did, so nobody's gesture changes until they ask for it here.
    //
    // A checkbox, not a two-value select. The explanation lives in the always-visible hint
    // below, one line, ASCII only. The persisted key and its boolean semantics: see
    // isZoomScrollUpZoomsInEnabled() / persistZoomScrollUpZoomsIn() below, the only read/write
    // sites, under kZoomScrollUpZoomsInKey.
    zoomScrollUpZoomsInToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(kZoomScrollUpZoomsInKey, true), juce::dontSendNotification);
    zoomScrollUpZoomsInToggle.setTooltip("When off, scrolling up zooms out. Applies to " + platformCommandKeyName() +
                                         " wheel zoom in the timeline and piano roll.");
    zoomScrollUpZoomsInToggle.onClick = [this] {
        persistZoomScrollUpZoomsIn(zoomScrollUpZoomsInToggle.getToggleState());
    };

    contentHost.addAndMakeVisible(zoomScrollUpZoomsInHint);
    // One line: short enough that styleMutedHintLabel's two-line-tall box never needs the
    // second line, but keeping the taller box means this row and naturalScrollingHint above it
    // stay pixel-identical in height.
    zoomScrollUpZoomsInHint.setText("When off, scrolling up zooms out. Applies to " + platformCommandKeyName() +
                                        " wheel zoom in the timeline and piano roll.",
                                    juce::dontSendNotification);
    styleMutedHintLabel(zoomScrollUpZoomsInHint);

    contentHost.addAndMakeVisible(pianoRollKeyLabelsToggle);
    // DEFAULT TRUE ("all"): matches PianoRollComponent::KeyLabelMode::AllNotes, its own default,
    // so an install that never opens this tab sees no change.
    pianoRollKeyLabelsToggle.setToggleState(
        appProperties.getUserSettings()->getValue(kPianoRollKeyLabelsKey, "all").equalsIgnoreCase("all"),
        juce::dontSendNotification);
    pianoRollKeyLabelsToggle.setTooltip("On labels every key in the piano roll's keys column. Off labels only the Cs.");
    pianoRollKeyLabelsToggle.onClick = [this] {
        persistPianoRollKeyLabelMode(pianoRollKeyLabelsToggle.getToggleState());
    };

    contentHost.addAndMakeVisible(autosaveEnabledToggle);
    // DEFAULT TRUE: autosave is a safety net, not an opt-in — see kAutosaveEnabledKey above.
    autosaveEnabledToggle.setToggleState(appProperties.getUserSettings()->getBoolValue(kAutosaveEnabledKey, true),
                                         juce::dontSendNotification);
    autosaveEnabledToggle.setTooltip("Periodically saves an in-progress copy of the open project to a separate "
                                     "file, offered for recovery next time it's opened. Never overwrites your own "
                                     "saved file.");
    autosaveEnabledToggle.onClick = [this] { persistAutosaveEnabled(autosaveEnabledToggle.getToggleState()); };

    contentHost.addAndMakeVisible(autosaveIntervalLabel);
    autosaveIntervalLabel.setText("Every:", juce::dontSendNotification);
    autosaveIntervalLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(autosaveIntervalEditor);
    autosaveIntervalEditor.setTitle("Autosave interval in minutes");
    autosaveIntervalEditor.setMultiLine(false);
    autosaveIntervalEditor.setReturnKeyStartsNewLine(false);
    autosaveIntervalEditor.setSelectAllWhenFocused(true);
    autosaveIntervalEditor.setJustification(juce::Justification::centred);
    autosaveIntervalEditor.setInputRestrictions(3, "0123456789"); // digits only; 3 chars covers 120 with headroom
    autosaveIntervalEditor.setTooltip("How often autosave writes the recovery copy while the project has unsaved "
                                      "changes, in minutes. Any exact value from 1 to 120.");
    autosaveIntervalEditor.setText(juce::String(appProperties.getUserSettings()->getIntValue(
                                       kAutosaveIntervalMinutesKey, kDefaultAutosaveIntervalMinutes)),
                                   juce::dontSendNotification);
    // Commit on BOTH Return and focus-lost - clicking away without pressing Return must not silently
    // discard (or worse, leave unpersisted) whatever was typed, the same reasoning ModuleComponent's
    // titleEditor commits on both. The clamp always writes a valid value back into the field, so an
    // out-of-range or emptied entry (getIntValue() reads an empty string as 0) snaps visibly to
    // something valid rather than persisting garbage.
    auto commitAutosaveInterval = [this] {
        const int clamped = juce::jlimit(1, 120, autosaveIntervalEditor.getText().getIntValue());
        autosaveIntervalEditor.setText(juce::String(clamped), juce::dontSendNotification);
        persistAutosaveIntervalMinutes(clamped);
    };
    autosaveIntervalEditor.onReturnKey = commitAutosaveInterval;
    autosaveIntervalEditor.onFocusLost = commitAutosaveInterval;

    contentHost.addAndMakeVisible(autosaveIntervalUnitLabel);
    autosaveIntervalUnitLabel.setText("min", juce::dontSendNotification);
    autosaveIntervalUnitLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(autosaveBackupCountLabel);
    autosaveBackupCountLabel.setText("Keep:", juce::dontSendNotification);
    autosaveBackupCountLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(autosaveBackupCountEditor);
    autosaveBackupCountEditor.setTitle("Autosave backups to keep");
    autosaveBackupCountEditor.setMultiLine(false);
    autosaveBackupCountEditor.setReturnKeyStartsNewLine(false);
    autosaveBackupCountEditor.setSelectAllWhenFocused(true);
    autosaveBackupCountEditor.setJustification(juce::Justification::centred);
    autosaveBackupCountEditor.setInputRestrictions(3, "0123456789"); // digits only; 3 chars covers 50 with headroom
    autosaveBackupCountEditor.setTooltip("How many previous autosave snapshots to keep on disk as numbered backups, "
                                         "in addition to the most recent one. 0 keeps no history. Any exact value "
                                         "from 0 to 50.");
    autosaveBackupCountEditor.setText(juce::String(appProperties.getUserSettings()->getIntValue(
                                          kAutosaveBackupCountKey, kDefaultAutosaveBackupCount)),
                                      juce::dontSendNotification);
    auto commitAutosaveBackupCount = [this] {
        const int clamped = juce::jlimit(0, 50, autosaveBackupCountEditor.getText().getIntValue());
        autosaveBackupCountEditor.setText(juce::String(clamped), juce::dontSendNotification);
        persistAutosaveBackupCount(clamped);
    };
    autosaveBackupCountEditor.onReturnKey = commitAutosaveBackupCount;
    autosaveBackupCountEditor.onFocusLost = commitAutosaveBackupCount;

    contentHost.addAndMakeVisible(autosaveBackupCountUnitLabel);
    autosaveBackupCountUnitLabel.setText("backups", juce::dontSendNotification);
    autosaveBackupCountUnitLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    setupMixerDefaultTrackPresetControls();
}

void PreferencesSettingsTab::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

// Painted by ContentHost (the viewport's viewed component), so the group-separator hairlines scroll
// along with the groups they separate - the same owner-delegation idiom ShortcutsSettingsTab's
// RowsHost uses for its section chrome. Runs in the host's own content coordinates, exactly the
// space layoutContent lays each divider into.
void PreferencesSettingsTab::paintContent(juce::Graphics& g) {
    // Group separators. Drawn from the text colour at low alpha rather than a theme token so the
    // rule stays legible on both light and dark themes without needing one of its own.
    const auto ruleColour = findColour(juce::Label::textColourId).withAlpha(kDividerAlpha);
    for (size_t i = 0; i < dividerBounds.size(); ++i) {
        g.setColour(i < dividerAlphas_.size() ? ruleColour.withMultipliedAlpha(dividerAlphas_[i]) : ruleColour);
        g.fillRect(dividerBounds[i]);
    }
}

void PreferencesSettingsTab::resized() {
    auto bounds = getLocalBounds().reduced(12);

    // Title and the search field stay pinned above the scroll view - the controls that decide
    // WHICH groups are on screen must never scroll out of reach, exactly as the Keyboard
    // Shortcuts tab keeps its own title/search/collapse strip out of its scrolled region.
    titleLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(8);
    auto pickerRow = bounds.removeFromTop(26);
    // Picker row: the drop-down takes exactly the width its longest entry needs, so the filter field
    // gets the rest and can show its whole hint at the Settings window's minimum width.
    categoryCombo.setBounds(
        pickerRow.removeFromLeft(synth::theme::AppLookAndFeel::comboBoxWidthToFitItems(categoryCombo)));
    pickerRow.removeFromLeft(8);
    searchField.setBounds(pickerRow);
    bounds.removeFromTop(8);
    // The All view (with no filter) has one fold-all strip, pinned top-right of the rows; every other
    // view gives that height back.
    const bool showFoldAll = sectionHeadersActive();
    foldAllButton.setVisible(showFoldAll);
    if (showFoldAll) {
        foldAllButton.setAllFolded(areAllSectionsCollapsed());
        foldAllButton.setBounds(bounds.removeFromTop(synth::ui::FoldAllButton::kStripHeight));
    }
    bounds.removeFromTop(4);

    // Everything below is scrolled content: the viewport clips it and shows a vertical scrollbar
    // when it overflows. Rows are laid out to the viewport width minus its scrollbar
    // gutter, so a control never runs under the thumb; reserving the gutter unconditionally
    // keeps the layout independent of whether the bar shows this very pass.
    contentViewport.setBounds(bounds);
    const int contentWidth = juce::jmax(0, contentViewport.getWidth() - contentViewport.getScrollBarThickness());
    std::fill(std::begin(sectionStartY), std::end(sectionStartY), -1);
    layoutContent(contentWidth);
    placeSectionHeaders(contentWidth);
    squashFadingGroups();
}

void PreferencesSettingsTab::layoutContent(int contentWidth) {
    dividerBounds.clear();
    laidOutGroups_.clear();

    // ---- Live filter -----------------------------------------------------------------------
    //
    // Each of the groups below is a row for filtering purposes: a group matches when every word of the
    // query occurs in its components' button/label/tooltip text (synth::ui::searchMatches). A query searches
    // EVERY category; an empty query shows only the selected category's groups (each layout unit
    // sets layoutCategory before its groups, so groupMatches can tell whose turn it is).
    const juce::String query = searchQuery;

    auto textOf = [](juce::Component& c) {
        // getTooltip() is not const on juce::SettableTooltipClient, hence the non-const parameter —
        // resized() itself is non-const, so there is nothing this actually mutates.
        juce::String s;
        if (auto* b = dynamic_cast<juce::Button*>(&c))
            s << b->getButtonText() << " ";
        if (auto* l = dynamic_cast<juce::Label*>(&c))
            s << l->getText() << " ";
        // No juce::ComboBox branch: the zoom row is a checkbox whose button text ("Scroll up to
        // zoom in") already contains "zoom" via the juce::Button branch above.
        if (auto* t = dynamic_cast<juce::SettableTooltipClient*>(&c))
            s << t->getTooltip() << " ";
        return s;
    };
    auto groupTarget = [&](std::initializer_list<juce::Component*> comps) {
        if (query.isEmpty())
            return categoryShown(layoutCategory);
        // One haystack per group, so every word of a multi-word query may land on a different control.
        juce::String haystack;
        for (auto* c : comps)
            haystack << textOf(*c) << " ";
        return synth::ui::searchMatches(haystack, query);
    };
    // A group that is going away keeps its slot (and its components) while it fades; fadeGroup answers true for it.
    auto groupMatches = [&](std::initializer_list<juce::Component*> comps) {
        return fadeGroup(comps, groupTarget(comps));
    };
    // The fades own visibility now (fadeGroup, called from groupMatches above); the closure stays so the layout
    // units keep their one shape.
    auto setGroupVisible = [](std::initializer_list<juce::Component*>, bool) {};
    // Each group is laid out top-down in CONTENT coordinates, accumulating a running y; the
    // host's total height (set at the end) is whatever the visible groups need, and the viewport
    // scrolls when that exceeds the visible area. addDivider reserves a hairline between two
    // visible groups, in the same content space.
    int y = 0;
    bool pendingDivider = false;
    auto addDivider = [&] {
        y += 10;
        dividerBounds.push_back(juce::Rectangle<int>{0, y, contentWidth, 1});
        y += 11;
    };
    auto beginGroup = [&](bool visible) {
        if (visible && pendingDivider)
            addDivider();
        if (visible)
            pendingDivider = false;
    };
    layoutGraphGroups(y, contentWidth, pendingDivider, groupMatches, setGroupVisible, beginGroup);
    layoutTimelineGroups(y, contentWidth, pendingDivider, groupMatches, setGroupVisible, beginGroup);
    layoutAutosaveGroup(y, contentWidth, pendingDivider, groupMatches, setGroupVisible, beginGroup);
    // Mixer rows, then the chain they head (placement, panel-detach mode, MIDI Remote).
    layoutMixerGroups(y, contentWidth, pendingDivider, groupMatches, setGroupVisible, beginGroup);
    // Size the content host to whatever the visible groups consumed; the viewport scrolls it.
    // Width spans the full viewport so the dividers reach the edges; the scrollbar
    // gutter is already excluded from contentWidth.
    contentHost.setBounds(0, 0, juce::jmax(contentWidth, contentViewport.getWidth()), juce::jmax(y, 1));
    contentHost.repaint();
}

void PreferencesSettingsTab::applySearchFilter(const juce::String& query) {
    searchQuery = query.trim();
    // A query searches every category, so the picker has nothing to say until it is cleared.
    categoryCombo.setEnabled(searchQuery.isEmpty());
    resized();
    repaint();
}

void PreferencesSettingsTab::setSearchFilterForTest(const juce::String& query) {
    // dontSendNotification + an explicit applySearchFilter() call, mirroring
    // ModuleLibraryComponent::setSearchText — deterministic regardless of whether juce::TextEditor's
    // own change notification happens to run synchronously in a headless test.
    searchField.setText(query, juce::dontSendNotification);
    applySearchFilter(query);
}

void PreferencesSettingsTab::setGraphEditor(GraphEditor* ge) {
    graphEditor = ge;
    if (graphEditor == nullptr)
        return;
    graphEditor->getSmartConnections().setSmartConnectionMode(modeFromComboId(smartConnectionCombo.getSelectedId()));
    graphEditor->getSmartConnections().setSideBySideMode(sideBySideFromComboId(sideBySideCombo_.getSelectedId()));
    graphEditor->setDoubleClickPortDisconnectEnabled(doubleClickDisconnectToggle.getToggleState());
    graphEditor->setReconnectChainOnDeleteEnabled(reconnectChainOnDeleteToggle.getToggleState());
    graphEditor->setAlignmentGuidesEnabled(alignmentGuideToggle.getToggleState());
    graphEditor->setDefaultDualIOForNewModules(defaultDualIOToggle.getToggleState());
    graphEditor->setDualIOPerModuleOverrides(dualIOPerModuleOverrides);
    graphEditor->setMacroAutoPortPreference(macroAutoPortPreferenceFromComboId(macroAutoPortCombo_.getSelectedId()));
    graphEditor->setAutoCreateMacroPortsOnDragEnabled(macroAutoCreatePortsOnDragToggle.getToggleState());
    graphEditor->setAutoDeleteMacroPortsOnLastCableEnabled(macroAutoDeletePortsOnLastCableToggle.getToggleState());
    graphEditor->setSpliceCableOnMacroPortDeleteEnabled(macroSpliceCableOnPortDeleteToggle.getToggleState());
    graphEditor->setMacroDragWithoutCmdEnabled(macroDragWithoutCmdToggle.getToggleState());
    graphEditor->setMoveMacroOnHullDragEnabled(moveMacroOnHullDragToggle.getToggleState());
    graphEditor->getMacroController().setPackMacrosOnCollapse(packMacrosOnCollapseToggle.getToggleState());
    graphEditor->getMacroController().setTidyCanvasOnPack(tidyCanvasOnPackToggle.getToggleState());
    graphEditor->setAutoCreateChannelOnConnectEnabled(mixerAutoCreateChannelOnConnectToggle.getToggleState());
}
