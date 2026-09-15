#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>

// Concern: construction, layout, search filter, and the smart-connection-mode / macro-
// auto-port-preference combo-id helpers shared with the getter/setter units.

namespace {
// Group-separator alpha. Softened from 0.18: at that contrast the hairlines read as table borders
// and boxed each preference in, which is the same complaint that produced the gentler rule under
// the Keyboard Shortcuts tab's section headers (see its kDividerAlpha — keep the two in step).
constexpr float kDividerAlpha = 0.12f;

// Height for a muted hint label under a preference row (round 5 fix): enough for TWO lines at the
// hint's 11.5pt font, so text wider than the row wraps instead of being horizontally squeezed —
// the 18px the two hints used before this only fit one line, and neither hint's text is short
// enough to actually be one line at the tab's real width.
constexpr int kHintHeight = 32;
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
    // Round 4 follow-up: without this, searchField below — a juce::TextEditor, and the first
    // focus-wanting descendant in this tab — auto-grabs keyboard focus the moment the Settings
    // DialogWindow's peer first gains OS focus. ComponentPeer::handleFocusGain() calls
    // grabKeyboardFocus() on the window's root component whenever a brand-new peer is shown with
    // nothing previously focused; since PreferencesSettingsTab itself didn't want focus, that call
    // fell through to KeyboardFocusTraverser::getDefaultComponent(), which returns the first
    // wants-focus child it finds in traversal order — searchField, purely because it happens to be
    // the first text field added. Declaring the TAB ITSELF as a focus target intercepts that
    // traversal one level up: takeKeyboardFocus() short-circuits onto the first component that
    // wants focus without descending further, so the tab (inert, no visible caret) absorbs the
    // opening grab instead of the search field. Exact same fix, same reason, as
    // ShortcutsSettingsTab's own setWantsKeyboardFocus(true) for its sibling search box — see that
    // constructor. Does NOT affect clicking directly into the field: TextEditor's own
    // wantsKeyboardFocus is untouched, so a click still focuses it normally.
    setWantsKeyboardFocus(true);

    addAndMakeVisible(titleLabel);
    titleLabel.setText("Preferences", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));

    // Live filter (round 3 follow-up): styled like ModuleLibraryComponent's own search box, which
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
    // region, but the groups scroll when they outgrow the window instead of getting clipped (T157).
    // contentHost's own size is computed in layoutContent; the viewport gives it a vertical scrollbar.
    addAndMakeVisible(contentViewport);
    contentViewport.setViewedComponent(&contentHost, false);
    contentViewport.setScrollBarsShown(true, false);

    contentHost.addAndMakeVisible(smartConnectionLabel);
    smartConnectionLabel.setText("Smart connections:", juce::dontSendNotification);
    smartConnectionLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    contentHost.addAndMakeVisible(smartConnectionCombo);
    smartConnectionCombo.addItem("Off", 1);
    smartConnectionCombo.addItem("New modules only", 2);
    smartConnectionCombo.addItem("When main I/O is free", 3);
    smartConnectionCombo.addItem("All module moves", 4);
    // "Ctrl" is spelled literally rather than through platformCommandKeyName(): the insert modifier
    // is the Control key on every platform, macOS included, precisely because Cmd already means
    // additive selection there.
    smartConnectionCombo.setTooltip("Suggest cables to nearby modules while placing or moving a card. "
                                    "Hold Ctrl while dragging to insert the module into an existing "
                                    "cable instead of adding a new one.");
    {
        const auto mode = GraphEditor::smartConnectionModeFromString(
            appProperties.getUserSettings()->getValue("smartConnectionMode", "NewAndUnwired"));
        smartConnectionCombo.setSelectedId(comboIdFromMode(mode), juce::dontSendNotification);
    }
    smartConnectionCombo.onChange = [this] {
        persistSmartConnectionMode(modeFromComboId(smartConnectionCombo.getSelectedId()));
    };

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

    // T148 (docs/macros_implementation.md §7 item 9): auto-create/auto-delete are plain on/off, unlike the
    // tri-state preference above — that one defaults to "ask" because it replaced pre-existing
    // silent behaviour; these two are brand-new automations the founder asked to ship ON by
    // default, with a plain escape hatch. Same idiom as doubleClickDisconnectToggle above.
    contentHost.addAndMakeVisible(macroAutoCreatePortsOnDragToggle);
    macroAutoCreatePortsOnDragToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue("macroAutoCreatePortsOnDrag", true), juce::dontSendNotification);
    macroAutoCreatePortsOnDragToggle.setTooltip(
        "When on (the default), dragging a cable across an expanded macro's boundary automatically "
        "creates a matching Mono port and wires it, instead of connecting straight through to the "
        "interior member.");
    macroAutoCreatePortsOnDragToggle.onClick = [this] {
        persistMacroAutoCreatePortsOnDrag(macroAutoCreatePortsOnDragToggle.getToggleState());
    };

    contentHost.addAndMakeVisible(macroAutoDeletePortsOnLastCableToggle);
    macroAutoDeletePortsOnLastCableToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue("macroAutoDeletePortsOnLastCable", true),
        juce::dontSendNotification);
    macroAutoDeletePortsOnLastCableToggle.setTooltip(
        "When on (the default), a macro port is automatically removed once its last cable is "
        "disconnected. When off, a cable-less port stays in place until removed by hand (Configure "
        "I/O or the port's own right-click Delete Port).");
    macroAutoDeletePortsOnLastCableToggle.onClick = [this] {
        persistMacroAutoDeletePortsOnLastCable(macroAutoDeletePortsOnLastCableToggle.getToggleState());
    };

    // T184 (P9-3c, docs/mixer.md §5.2 "main workflow"): auto-create a mixer channel when a MIDI
    // cable from a Track In node connects to an instrument/macro whose audio reaches the output
    // with no channel yet. Same "plain on/off, ON by default" shape as the two T148 toggles above.
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

    contentHost.addAndMakeVisible(doubleClickSpansLocatorsToggle);
    // DEFAULT TRUE, same idiom as the rows above.
    doubleClickSpansLocatorsToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(kTimelineDoubleClickSpansLocatorsKey, true),
        juce::dontSendNotification);
    doubleClickSpansLocatorsToggle.setTooltip(
        "When on (the default), double-clicking empty lane space INSIDE the loop locators creates a clip spanning "
        "them. Outside the locators - or with no locators set - you still get a one-bar clip. Turn it off to always "
        "get one bar.");
    doubleClickSpansLocatorsToggle.onClick = [this] {
        persistDoubleClickSpansLocators(doubleClickSpansLocatorsToggle.getToggleState());
    };

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
    // Round 6: back to a checkbox after round 5's labelled dropdown ("Zoom direction:" + two
    // options) drew a second round of pushback -- the user does not want two-value selects. The
    // explanation that used to live in the toggle's own tooltip now lives in the always-visible
    // hint below instead, one line, ASCII only. Persisted key and its boolean semantics are
    // UNCHANGED across every round: see isZoomScrollUpZoomsInEnabled() /
    // persistZoomScrollUpZoomsIn() below, still the only read/write sites, still under
    // kZoomScrollUpZoomsInKey.
    zoomScrollUpZoomsInToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue(kZoomScrollUpZoomsInKey, true), juce::dontSendNotification);
    zoomScrollUpZoomsInToggle.setTooltip("When off, scrolling up zooms out. Applies to " + platformCommandKeyName() +
                                         " wheel zoom in the timeline and piano roll.");
    zoomScrollUpZoomsInToggle.onClick = [this] {
        persistZoomScrollUpZoomsIn(zoomScrollUpZoomsInToggle.getToggleState());
    };

    contentHost.addAndMakeVisible(zoomScrollUpZoomsInHint);
    // One line (round 6): short enough that styleMutedHintLabel's two-line-tall box (kept from
    // round 5's layout fix) never needs the second line, but the taller box is harmless and keeping
    // it means this row and naturalScrollingHint above it stay pixel-identical in height.
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
    g.setColour(findColour(juce::Label::textColourId).withAlpha(kDividerAlpha));
    for (const auto& divider : dividerBounds)
        g.fillRect(divider);
}

void PreferencesSettingsTab::resized() {
    auto bounds = getLocalBounds().reduced(12);

    // Title and the search field stay pinned above the scroll view - the controls that decide
    // WHICH groups are on screen must never scroll out of reach, exactly as the Keyboard
    // Shortcuts tab keeps its own title/search/collapse strip out of its scrolled region.
    titleLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(8);
    searchField.setBounds(bounds.removeFromTop(26));
    bounds.removeFromTop(12);

    // Everything below is scrolled content: the viewport clips it and shows a vertical scrollbar
    // when it overflows (T157). Rows are laid out to the viewport width minus its scrollbar
    // gutter, so a control never runs under the thumb; reserving the gutter unconditionally
    // keeps the layout independent of whether the bar shows this very pass.
    contentViewport.setBounds(bounds);
    layoutContent(juce::jmax(0, contentViewport.getWidth() - contentViewport.getScrollBarThickness()));
}

void PreferencesSettingsTab::layoutContent(int contentWidth) {
    dividerBounds.clear();

    // ---- Live filter (round 3 follow-up item 2) --------------------------------------------
    //
    // Each of the groups below is a row for filtering purposes: a group matches when ANY of its
    // components button/label/tooltip text contains the query (case-insensitive); an empty query
    // matches everything, so an untouched search field reproduces the usual bounds.
    const juce::String query = searchQuery;

    auto textOf = [](juce::Component& c) {
        // getTooltip() is not const on juce::SettableTooltipClient, hence the non-const parameter —
        // resized() itself is non-const, so there is nothing this actually mutates.
        juce::String s;
        if (auto* b = dynamic_cast<juce::Button*>(&c))
            s << b->getButtonText() << " ";
        if (auto* l = dynamic_cast<juce::Label*>(&c))
            s << l->getText() << " ";
        // No juce::ComboBox branch: that was added in round 5 specifically so the zoom-direction
        // dropdown's row was findable by "zoom" regardless of which option was selected. Round 6
        // reverted that row to a checkbox — its button text ("Scroll up to zoom in") already
        // contains "zoom" via the juce::Button branch above, so no combo special-case is needed.
        if (auto* t = dynamic_cast<juce::SettableTooltipClient*>(&c))
            s << t->getTooltip() << " ";
        return s;
    };
    auto groupMatches = [&](std::initializer_list<juce::Component*> comps) {
        if (query.isEmpty())
            return true;
        for (auto* c : comps)
            if (textOf(*c).containsIgnoreCase(query))
                return true;
        return false;
    };
    auto setGroupVisible = [](std::initializer_list<juce::Component*> comps, bool visible) {
        for (auto* c : comps)
            c->setVisible(visible);
    };
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
    // Group 1: smart connections
    {
        const bool visible = groupMatches({&smartConnectionLabel, &smartConnectionCombo});
        setGroupVisible({&smartConnectionLabel, &smartConnectionCombo}, visible);
        beginGroup(visible);
        if (visible) {
            juce::Rectangle<int> smartRow(0, y, contentWidth, 24);
            smartConnectionLabel.setBounds(smartRow.removeFromLeft(160));
            smartConnectionCombo.setBounds(smartRow.removeFromLeft(220));
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 2: double-click disconnect
    {
        const bool visible = groupMatches({&doubleClickDisconnectToggle});
        setGroupVisible({&doubleClickDisconnectToggle}, visible);
        beginGroup(visible);
        if (visible) {
            doubleClickDisconnectToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 3: alignment guides
    {
        const bool visible = groupMatches({&alignmentGuideToggle});
        setGroupVisible({&alignmentGuideToggle}, visible);
        beginGroup(visible);
        if (visible) {
            alignmentGuideToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 4: Dual I/O (one line, one row - see the toggle's declaration comment).
    {
        const bool visible = groupMatches({&defaultDualIOToggle, &perModuleDefaultsButton});
        setGroupVisible({&defaultDualIOToggle, &perModuleDefaultsButton}, visible);
        beginGroup(visible);
        if (visible) {
            juce::Rectangle<int> dualIORow(0, y, contentWidth, 24);
            perModuleDefaultsButton.changeWidthToFitText(24);
            const int buttonWidth = juce::jmax(perModuleDefaultsButton.getWidth(), 160);
            perModuleDefaultsButton.setBounds(dualIORow.removeFromRight(buttonWidth));
            dualIORow.removeFromRight(12);
            defaultDualIOToggle.setBounds(dualIORow);
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 4b: macro auto-port preference (founder-review fix F5).
    {
        const bool visible = groupMatches({&macroAutoPortLabel_, &macroAutoPortCombo_});
        setGroupVisible({&macroAutoPortLabel_, &macroAutoPortCombo_}, visible);
        beginGroup(visible);
        if (visible) {
            juce::Rectangle<int> row(0, y, contentWidth, 24);
            macroAutoPortLabel_.setBounds(row.removeFromLeft(160));
            macroAutoPortCombo_.setBounds(row.removeFromLeft(220));
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 4c: T148 macro auto-create/auto-delete toggles.
    {
        const bool visible = groupMatches({&macroAutoCreatePortsOnDragToggle, &macroAutoDeletePortsOnLastCableToggle});
        setGroupVisible({&macroAutoCreatePortsOnDragToggle, &macroAutoDeletePortsOnLastCableToggle}, visible);
        beginGroup(visible);
        if (visible) {
            macroAutoCreatePortsOnDragToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
            macroAutoDeletePortsOnLastCableToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 4d: T184 mixer auto-create-channel-on-connect toggle.
    {
        const bool visible = groupMatches({&mixerAutoCreateChannelOnConnectToggle});
        setGroupVisible({&mixerAutoCreateChannelOnConnectToggle}, visible);
        beginGroup(visible);
        if (visible) {
            mixerAutoCreateChannelOnConnectToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 5: the two loop-locator toggles (no divider between them).
    {
        const bool visible = groupMatches({&loopSelectionArmsToggle, &doubleClickSpansLocatorsToggle});
        setGroupVisible({&loopSelectionArmsToggle, &doubleClickSpansLocatorsToggle}, visible);
        beginGroup(visible);
        if (visible) {
            loopSelectionArmsToggle.setBounds({0, y, contentWidth, 24});
            y += 34;
            doubleClickSpansLocatorsToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 6: the two wheel-direction toggles + their hints.
    {
        const bool visible = groupMatches(
            {&naturalScrollingToggle, &naturalScrollingHint, &zoomScrollUpZoomsInToggle, &zoomScrollUpZoomsInHint});
        setGroupVisible(
            {&naturalScrollingToggle, &naturalScrollingHint, &zoomScrollUpZoomsInToggle, &zoomScrollUpZoomsInHint},
            visible);
        beginGroup(visible);
        if (visible) {
            naturalScrollingToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
            // Indented under the toggle it explains, so the hint reads as a caption.
            naturalScrollingHint.setBounds({24, y, contentWidth - 24, kHintHeight});
            y += kHintHeight + 10;
            zoomScrollUpZoomsInToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
            zoomScrollUpZoomsInHint.setBounds({24, y, contentWidth - 24, kHintHeight});
            y += kHintHeight;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 7: piano roll key labels
    {
        const bool visible = groupMatches({&pianoRollKeyLabelsToggle});
        setGroupVisible({&pianoRollKeyLabelsToggle}, visible);
        beginGroup(visible);
        if (visible) {
            pianoRollKeyLabelsToggle.setBounds({0, y, contentWidth, 24});
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 8: autosave.
    {
        const std::initializer_list<juce::Component*> autosaveComps = {
            &autosaveEnabledToggle,       &autosaveIntervalLabel,    &autosaveIntervalEditor,
            &autosaveIntervalUnitLabel,   &autosaveBackupCountLabel, &autosaveBackupCountEditor,
            &autosaveBackupCountUnitLabel};
        const bool visible = groupMatches(autosaveComps);
        setGroupVisible(autosaveComps, visible);
        beginGroup(visible);
        if (visible) {
            juce::Rectangle<int> row(0, y, contentWidth, 24);
            autosaveEnabledToggle.setBounds(row.removeFromLeft(90));
            row.removeFromLeft(16);
            autosaveIntervalLabel.setBounds(row.removeFromLeft(40));
            row.removeFromLeft(4);
            autosaveIntervalEditor.setBounds(row.removeFromLeft(36));
            row.removeFromLeft(4);
            autosaveIntervalUnitLabel.setBounds(row.removeFromLeft(30));
            row.removeFromLeft(16);
            autosaveBackupCountLabel.setBounds(row.removeFromLeft(40));
            row.removeFromLeft(4);
            autosaveBackupCountEditor.setBounds(row.removeFromLeft(36));
            row.removeFromLeft(4);
            autosaveBackupCountUnitLabel.setBounds(row.removeFromLeft(55));
            y += 24;
        }
        pendingDivider = pendingDivider || visible;
    }
    // Group 9 (FRO13, P9-7, last - no divider after it):
    layoutMixerDefaultTrackPresetGroup(y, contentWidth, groupMatches, setGroupVisible, beginGroup);
    // Size the content host to whatever the visible groups consumed; the viewport scrolls it
    // (T157). Width spans the full viewport so the dividers reach the edges; the scrollbar
    // gutter is already excluded from contentWidth.
    contentHost.setBounds(0, 0, juce::jmax(contentWidth, contentViewport.getWidth()), juce::jmax(y, 1));
    contentHost.repaint();
}

void PreferencesSettingsTab::applySearchFilter(const juce::String& query) {
    searchQuery = query.trim();
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
    graphEditor->setSmartConnectionMode(modeFromComboId(smartConnectionCombo.getSelectedId()));
    graphEditor->setDoubleClickPortDisconnectEnabled(doubleClickDisconnectToggle.getToggleState());
    graphEditor->setAlignmentGuidesEnabled(alignmentGuideToggle.getToggleState());
    graphEditor->setDefaultDualIOForNewModules(defaultDualIOToggle.getToggleState());
    graphEditor->setDualIOPerModuleOverrides(dualIOPerModuleOverrides);
    graphEditor->setMacroAutoPortPreference(macroAutoPortPreferenceFromComboId(macroAutoPortCombo_.getSelectedId()));
    graphEditor->setAutoCreateMacroPortsOnDragEnabled(macroAutoCreatePortsOnDragToggle.getToggleState());
    graphEditor->setAutoDeleteMacroPortsOnLastCableEnabled(macroAutoDeletePortsOnLastCableToggle.getToggleState());
    graphEditor->setAutoCreateChannelOnConnectEnabled(mixerAutoCreateChannelOnConnectToggle.getToggleState());
}
