#include "PreferencesSettingsTab.h"
#include "../AI/AIStateMapper.h"
#include "../ShortcutManager.h"
#include "Theme/AppLookAndFeel.h"
#include <functional>

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

// Founder-review fix F5 (docs/macros.md §7 item 6.1/6.2). Duplicated from the constexpr
// GraphEditor::requestGroupSelectionIntoMacro() writes through propertiesFile_ directly for the
// "remember my choice" case (that modal can fire before this tab, or any Settings window, has
// ever been constructed) — the same "one-line string not worth a header dependency" reasoning
// kNaturalScrollingKey below documents, and the two writers MUST agree on the string values below.
// DEFAULT "ask" (Unset): a silent default of either auto-creating or dropping cables would change
// existing behaviour with no warning the first time this ships.
constexpr const char* kMacroAutoPortPreferenceKey = "macroAutoCreatePorts";

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

juce::String macroAutoPortPreferenceToString(GraphEditor::MacroAutoPortPreference pref) {
    switch (pref) {
    case GraphEditor::MacroAutoPortPreference::AutoCreatePorts:
        return "auto";
    case GraphEditor::MacroAutoPortPreference::LeaveCablesAsIs:
        return "leave";
    case GraphEditor::MacroAutoPortPreference::Unset:
    default:
        return "ask";
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

// Read at use time by TimelineClipLaneArea::locatorSpanForDoubleClick, and duplicated here for the
// same reason kNaturalScrollingKey above is. DEFAULT TRUE: authoring a clip that fills the loop you
// just set is the whole point of the feature, so it ships on and the toggle exists to turn it OFF
// for anyone who wants the plain one-bar clip back.
constexpr const char* kTimelineDoubleClickSpansLocatorsKey = "timelineDoubleClickSpansLocators";

// Autosave (P8-4). Read at use time by MainComponent::maybeAutosave every timerCallback() tick,
// duplicated here for the same reason kNaturalScrollingKey above is. DEFAULT ON at 2 minutes:
// autosave is a safety net, not an opt-in, so an install that never opens this tab still gets it.
constexpr const char* kAutosaveEnabledKey = "autosaveEnabled";
constexpr const char* kAutosaveIntervalMinutesKey = "autosaveIntervalMinutes";
constexpr int kDefaultAutosaveIntervalMinutes = 2;
// Cubase-style rotating backup history — see ProjectBundle::saveAutosave. Duplicated from
// MainComponent's own copy of this key/default for the same "one-line string not worth a header
// dependency" reason as kAutosaveEnabledKey above.
constexpr const char* kAutosaveBackupCountKey = "autosaveBackupCount";
constexpr int kDefaultAutosaveBackupCount = 5;

// Group-separator alpha. Softened from 0.18: at that contrast the hairlines read as table borders
// and boxed each preference in, which is the same complaint that produced the gentler rule under
// the Keyboard Shortcuts tab's section headers (see its kDividerAlpha — keep the two in step).
constexpr float kDividerAlpha = 0.12f;

// Height for a muted hint label under a preference row (round 5 fix): enough for TWO lines at the
// hint's 11.5pt font, so text wider than the row wraps instead of being horizontally squeezed —
// the 18px the two hints used before this only fit one line, and neither hint's text is short
// enough to actually be one line at the tab's real width.
constexpr int kHintHeight = 32;

// Per-module overrides of "defaultDualIOForNewModules", one compact JSON object: {"TypeName":
// true|false}. A type absent from the object follows the global default — that is the whole
// reason this is a JSON blob under one key rather than one key per module type, the same way
// ShortcutManager keeps its rebinds as one object instead of one property per action.
constexpr const char* kDualIOPerModuleDefaultsKey = "dualIOPerModuleDefaults";

// Combo item ids for the per-module popup's 3-state choice (0 is reserved by juce::ComboBox for
// "nothing selected", so these start at 1 like every other combo in this file).
constexpr int kDualIOChoiceFollowGlobal = 1;
constexpr int kDualIOChoiceAlwaysOn = 2;
constexpr int kDualIOChoiceAlwaysOff = 3;

// Every module type that carries the Dual I/O parameter — which ModuleBase's constructor grants from
// the module's channel shape (ModuleBase::StereoAudio): the FX modules plus the split-block voice
// modules (docs/fx_modules.md § Stereo I/O).
//
// DERIVED, never hand-listed: synth::AIStateMapper::dualIOCapableModuleTypes() probes the module
// factory and asks each module hasDualIOParameter(), so the popup below cannot go stale. It used to
// be a literal vector here, and the Ring Modulator was missing from it — the module supported a
// stereo pair but no row existed to set its default, and nothing failed.
//
// The names are factory keys, which is exactly what GraphEditor::addModuleAtCanvasPosition receives
// as the "name" it creates and what applyDefaultDualIOForNewModule matches overrides against.
const std::vector<juce::String>& dualIOModuleTypes() {
    static const std::vector<juce::String> types = [] {
        std::vector<juce::String> v;
        for (const auto& name : synth::AIStateMapper::dualIOCapableModuleTypes())
            v.push_back(name);
        return v;
    }();
    return types;
}

// Popup content for the "Per-module I/O defaults..." button: a plain themed column, one
// juce::Label + juce::ComboBox pair per module type, flat children (no per-row wrapper) so tests
// can find the Nth juce::ComboBox the same way ClickingTheToggleReachesTheEditorAndNewModules
// finds the Dual I/O ToggleButton — by walking getChildren() and dynamic_cast. No search box and
// no viewport, unlike MidiDestinationPicker: one row per stereo-capable module at kRowHeight fits
// comfortably inside a CallOutBox on any real screen, and a popup this rarely opened does not earn
// that rig.
class DualIOPerModulePopupContent : public juce::Component {
public:
    DualIOPerModulePopupContent(const std::vector<juce::String>& moduleTypes,
                                const std::function<std::optional<bool>(const juce::String&)>& getOverride,
                                const std::function<void(const juce::String&, std::optional<bool>)>& setOverride) {
        rows.reserve(moduleTypes.size());
        for (const auto& type : moduleTypes) {
            auto label = std::make_unique<juce::Label>();
            label->setText(type, juce::dontSendNotification);
            label->setFont(juce::Font(juce::FontOptions(12.5f)));
            addAndMakeVisible(*label);

            auto combo = std::make_unique<juce::ComboBox>();
            combo->addItem("Follow global", kDualIOChoiceFollowGlobal);
            combo->addItem("Always on", kDualIOChoiceAlwaysOn);
            combo->addItem("Always off", kDualIOChoiceAlwaysOff);
            const auto current = getOverride(type);
            combo->setSelectedId(current.has_value() ? (*current ? kDualIOChoiceAlwaysOn : kDualIOChoiceAlwaysOff)
                                                     : kDualIOChoiceFollowGlobal,
                                 juce::dontSendNotification);
            combo->onChange = [c = combo.get(), type, setOverride] {
                switch (c->getSelectedId()) {
                case kDualIOChoiceAlwaysOn:
                    setOverride(type, true);
                    break;
                case kDualIOChoiceAlwaysOff:
                    setOverride(type, false);
                    break;
                default:
                    setOverride(type, std::nullopt);
                    break;
                }
            };
            addAndMakeVisible(*combo);

            rows.push_back({std::move(label), std::move(combo)});
        }
        setSize(kWidth, kPadding * 2 + kRowHeight * static_cast<int>(rows.size()));
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(kPadding);
        for (auto& row : rows) {
            auto r = bounds.removeFromTop(kRowHeight);
            row.label->setBounds(r.removeFromLeft(kLabelWidth));
            row.combo->setBounds(r);
        }
    }

    // Opaque themed panel, mirroring MidiDestinationPicker's paint(): a CallOutBox launched with no
    // parent (see this file's onClick lambda) becomes a top-level window that does not necessarily
    // inherit synth::theme::AppLookAndFeel.
    void paint(juce::Graphics& g) override {
        juce::Colour bg = juce::Colours::darkgrey.darker(0.4f);
        juce::Colour border = juce::Colours::grey.darker();
        float radius = 6.0f;
        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
            const auto& c = lf->getTheme().colors;
            bg = c.surface;
            border = c.border;
            radius = lf->getTheme().metrics.cornerRadius;
        }
        auto b = getLocalBounds().toFloat();
        g.setColour(bg);
        g.fillRoundedRectangle(b, radius);
        g.setColour(border);
        g.drawRoundedRectangle(b.reduced(0.5f), radius, 1.0f);
    }

private:
    static constexpr int kWidth = 320;
    static constexpr int kPadding = 8;
    static constexpr int kRowHeight = 26;
    static constexpr int kLabelWidth = 150;

    struct Row {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::ComboBox> combo;
    };
    std::vector<Row> rows;
};

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

    addAndMakeVisible(smartConnectionLabel);
    smartConnectionLabel.setText("Smart connections:", juce::dontSendNotification);
    smartConnectionLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    addAndMakeVisible(smartConnectionCombo);
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
    defaultDualIOToggle.setTooltip("Splits the audio jacks on every stereo-capable module - FX, Voice Mixer output, "
                                   "Oscillator, Wavetable, Filter, VCA and Sampler. Applies to modules already on the "
                                   "canvas as well as new ones. Card heights do not change.");
    defaultDualIOToggle.onClick = [this] { persistDefaultDualIOForNewModules(defaultDualIOToggle.getToggleState()); };

    dualIOPerModuleOverrides = loadDualIOPerModuleOverrides(appProperties);

    addAndMakeVisible(perModuleDefaultsButton);
    perModuleDefaultsButton.setTooltip(
        "Per-module overrides of the Split Left/Right default above - Follow global, Always on, or Always off for "
        "each module type. Applies to modules created after the change, same as the toggle.");
    perModuleDefaultsButton.onClick = [this] {
        auto popup = buildDualIOPerModuleDefaultsPopup();
        juce::CallOutBox::launchAsynchronously(std::move(popup), perModuleDefaultsButton.getScreenBounds(), nullptr);
    };

    addAndMakeVisible(macroAutoPortLabel_);
    macroAutoPortLabel_.setText("Macro auto-ports:", juce::dontSendNotification);
    macroAutoPortLabel_.setFont(juce::Font(juce::FontOptions(13.0f)));

    addAndMakeVisible(macroAutoPortCombo_);
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

    addAndMakeVisible(loopSelectionArmsToggle);
    loopSelectionArmsToggle.setToggleState(
        appProperties.getUserSettings()->getBoolValue("timelineLoopSelectionArms", true), juce::dontSendNotification);
    loopSelectionArmsToggle.setTooltip(
        "When on, pressing P in the timeline both places the loop locators around the selection AND switches "
        "looping on. When off, P only places the locators (use L to toggle looping).");
    loopSelectionArmsToggle.onClick = [this] { persistLoopSelectionArms(loopSelectionArmsToggle.getToggleState()); };

    addAndMakeVisible(doubleClickSpansLocatorsToggle);
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
    styleMutedHintLabel(naturalScrollingHint);

    addAndMakeVisible(zoomScrollUpZoomsInToggle);
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

    addAndMakeVisible(zoomScrollUpZoomsInHint);
    // One line (round 6): short enough that styleMutedHintLabel's two-line-tall box (kept from
    // round 5's layout fix) never needs the second line, but the taller box is harmless and keeping
    // it means this row and naturalScrollingHint above it stay pixel-identical in height.
    zoomScrollUpZoomsInHint.setText("When off, scrolling up zooms out. Applies to " + platformCommandKeyName() +
                                        " wheel zoom in the timeline and piano roll.",
                                    juce::dontSendNotification);
    styleMutedHintLabel(zoomScrollUpZoomsInHint);

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

    addAndMakeVisible(autosaveEnabledToggle);
    // DEFAULT TRUE: autosave is a safety net, not an opt-in — see kAutosaveEnabledKey above.
    autosaveEnabledToggle.setToggleState(appProperties.getUserSettings()->getBoolValue(kAutosaveEnabledKey, true),
                                         juce::dontSendNotification);
    autosaveEnabledToggle.setTooltip("Periodically saves an in-progress copy of the open project to a separate "
                                     "file, offered for recovery next time it's opened. Never overwrites your own "
                                     "saved file.");
    autosaveEnabledToggle.onClick = [this] { persistAutosaveEnabled(autosaveEnabledToggle.getToggleState()); };

    addAndMakeVisible(autosaveIntervalLabel);
    autosaveIntervalLabel.setText("Every:", juce::dontSendNotification);
    autosaveIntervalLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    addAndMakeVisible(autosaveIntervalEditor);
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

    addAndMakeVisible(autosaveIntervalUnitLabel);
    autosaveIntervalUnitLabel.setText("min", juce::dontSendNotification);
    autosaveIntervalUnitLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    addAndMakeVisible(autosaveBackupCountLabel);
    autosaveBackupCountLabel.setText("Keep:", juce::dontSendNotification);
    autosaveBackupCountLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    addAndMakeVisible(autosaveBackupCountEditor);
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

    addAndMakeVisible(autosaveBackupCountUnitLabel);
    autosaveBackupCountUnitLabel.setText("backups", juce::dontSendNotification);
    autosaveBackupCountUnitLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
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

    titleLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(8);
    searchField.setBounds(bounds.removeFromTop(26));
    bounds.removeFromTop(12);

    // ---- Live filter (round 3 follow-up item 2) --------------------------------------------
    //
    // Each of the groups below is a "row" for filtering purposes — the same grouping the dividers
    // already draw, so filtering never needs a finer-grained concept of "row" than what the layout
    // already treats as one block. A group matches when ANY of its components' button text, label
    // text or tooltip contains the query (case-insensitive); an empty query matches everything, so
    // an untouched search field reproduces the exact bounds this function always produced.
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

    // Each group that wants a divider after it sets pendingDivider = true; the divider is only
    // actually drawn once a LATER group turns out to be visible (beginGroup below), so a filtered-
    // out group in between never leaves an orphan hairline over empty space, and the first/last
    // visible group never gets a leading/trailing one either.
    bool pendingDivider = false;
    auto addDivider = [this, &bounds] {
        bounds.removeFromTop(10);
        dividerBounds.push_back(bounds.removeFromTop(1));
        bounds.removeFromTop(10);
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
            auto smartRow = bounds.removeFromTop(24);
            smartConnectionLabel.setBounds(smartRow.removeFromLeft(160));
            smartConnectionCombo.setBounds(smartRow.removeFromLeft(220));
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 2: double-click disconnect
    {
        const bool visible = groupMatches({&doubleClickDisconnectToggle});
        setGroupVisible({&doubleClickDisconnectToggle}, visible);
        beginGroup(visible);
        if (visible)
            doubleClickDisconnectToggle.setBounds(bounds.removeFromTop(24));
        pendingDivider = pendingDivider || visible;
    }

    // Group 3: alignment guides
    {
        const bool visible = groupMatches({&alignmentGuideToggle});
        setGroupVisible({&alignmentGuideToggle}, visible);
        beginGroup(visible);
        if (visible)
            alignmentGuideToggle.setBounds(bounds.removeFromTop(24));
        pendingDivider = pendingDivider || visible;
    }

    // Group 4: Dual I/O (one line, one row — see the toggle's declaration comment). The button is
    // sized to its own text via changeWidthToFitText rather than a fixed guess, so the toggle keeps
    // as much of the row as it can for its own (longer) label.
    {
        const bool visible = groupMatches({&defaultDualIOToggle, &perModuleDefaultsButton});
        setGroupVisible({&defaultDualIOToggle, &perModuleDefaultsButton}, visible);
        beginGroup(visible);
        if (visible) {
            auto dualIORow = bounds.removeFromTop(24);
            perModuleDefaultsButton.changeWidthToFitText(24);
            const int buttonWidth = juce::jmax(perModuleDefaultsButton.getWidth(), 160);
            perModuleDefaultsButton.setBounds(dualIORow.removeFromRight(buttonWidth));
            dualIORow.removeFromRight(12);
            defaultDualIOToggle.setBounds(dualIORow);
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 4b: macro auto-port preference (founder-review fix F5, docs/macros.md §7 item 6.2).
    {
        const bool visible = groupMatches({&macroAutoPortLabel_, &macroAutoPortCombo_});
        setGroupVisible({&macroAutoPortLabel_, &macroAutoPortCombo_}, visible);
        beginGroup(visible);
        if (visible) {
            auto row = bounds.removeFromTop(24);
            macroAutoPortLabel_.setBounds(row.removeFromLeft(160));
            macroAutoPortCombo_.setBounds(row.removeFromLeft(220));
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 5: the two loop-locator toggles (no divider between them — see their declaration
    // comments). Grouped for filtering too: they read as one conversation, so a query matching
    // either keeps both rows together rather than splitting a pair that explains itself as a pair.
    {
        const bool visible = groupMatches({&loopSelectionArmsToggle, &doubleClickSpansLocatorsToggle});
        setGroupVisible({&loopSelectionArmsToggle, &doubleClickSpansLocatorsToggle}, visible);
        beginGroup(visible);
        if (visible) {
            loopSelectionArmsToggle.setBounds(bounds.removeFromTop(24));
            bounds.removeFromTop(10);
            doubleClickSpansLocatorsToggle.setBounds(bounds.removeFromTop(24));
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 6: the two wheel-direction toggles + their hints (no divider between them, same
    // "reads as one conversation" reasoning as group 5).
    {
        const bool visible = groupMatches(
            {&naturalScrollingToggle, &naturalScrollingHint, &zoomScrollUpZoomsInToggle, &zoomScrollUpZoomsInHint});
        setGroupVisible(
            {&naturalScrollingToggle, &naturalScrollingHint, &zoomScrollUpZoomsInToggle, &zoomScrollUpZoomsInHint},
            visible);
        beginGroup(visible);
        if (visible) {
            naturalScrollingToggle.setBounds(bounds.removeFromTop(24));
            // Indented under the toggle it explains, so the hint reads as a caption rather than as
            // another preference row. kHintHeight (not a single line's worth): round 5 fix for both
            // hints in this group — see styleMutedHintLabel.
            naturalScrollingHint.setBounds(bounds.removeFromTop(kHintHeight).withTrimmedLeft(24));
            bounds.removeFromTop(10);
            zoomScrollUpZoomsInToggle.setBounds(bounds.removeFromTop(24));
            zoomScrollUpZoomsInHint.setBounds(bounds.removeFromTop(kHintHeight).withTrimmedLeft(24));
        }
        pendingDivider = pendingDivider || visible;
    }

    // Group 7: piano roll key labels
    {
        const bool visible = groupMatches({&pianoRollKeyLabelsToggle});
        setGroupVisible({&pianoRollKeyLabelsToggle}, visible);
        beginGroup(visible);
        if (visible)
            pianoRollKeyLabelsToggle.setBounds(bounds.removeFromTop(24));
        pendingDivider = pendingDivider || visible;
    }

    // Group 8: autosave (last — no divider after it, filtered or not). One single row: the toggle,
    // then "Every: [field] min", then "Keep: [field] backups" — three independent statements read
    // together as one line rather than stacked as separate rows.
    {
        const std::initializer_list<juce::Component*> autosaveComps = {
            &autosaveEnabledToggle,       &autosaveIntervalLabel,    &autosaveIntervalEditor,
            &autosaveIntervalUnitLabel,   &autosaveBackupCountLabel, &autosaveBackupCountEditor,
            &autosaveBackupCountUnitLabel};
        const bool visible = groupMatches(autosaveComps);
        setGroupVisible(autosaveComps, visible);
        beginGroup(visible);
        if (visible) {
            auto row = bounds.removeFromTop(24);
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
        }
    }
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

bool PreferencesSettingsTab::isAutosaveEnabled() const { return autosaveEnabledToggle.getToggleState(); }

void PreferencesSettingsTab::setAutosaveEnabled(bool enabled) {
    autosaveEnabledToggle.setToggleState(enabled, juce::dontSendNotification);
    persistAutosaveEnabled(enabled);
}

void PreferencesSettingsTab::persistAutosaveEnabled(bool enabled) {
    appProperties.getUserSettings()->setValue(kAutosaveEnabledKey, enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    // No live push from here: MainComponent::maybeAutosave reads the key directly on every tick, the
    // same "read at use time" idiom persistDoubleClickSpansLocators documents above — there is
    // nothing to push, since the very next tick already sees the new value.
}

int PreferencesSettingsTab::getAutosaveIntervalMinutes() const {
    return autosaveIntervalEditor.getText().getIntValue();
}

void PreferencesSettingsTab::setAutosaveIntervalMinutes(int minutes) {
    const int clamped = juce::jlimit(1, 120, minutes);
    autosaveIntervalEditor.setText(juce::String(clamped), juce::dontSendNotification);
    persistAutosaveIntervalMinutes(clamped);
}

void PreferencesSettingsTab::persistAutosaveIntervalMinutes(int minutes) {
    appProperties.getUserSettings()->setValue(kAutosaveIntervalMinutesKey, minutes);
    appProperties.getUserSettings()->saveIfNeeded();
}

int PreferencesSettingsTab::getAutosaveBackupCount() const { return autosaveBackupCountEditor.getText().getIntValue(); }

void PreferencesSettingsTab::setAutosaveBackupCount(int count) {
    const int clamped = juce::jlimit(0, 50, count);
    autosaveBackupCountEditor.setText(juce::String(clamped), juce::dontSendNotification);
    persistAutosaveBackupCount(clamped);
}

void PreferencesSettingsTab::persistAutosaveBackupCount(int count) {
    appProperties.getUserSettings()->setValue(kAutosaveBackupCountKey, count);
    appProperties.getUserSettings()->saveIfNeeded();
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

GraphEditor::MacroAutoPortPreference PreferencesSettingsTab::getMacroAutoPortPreference() const {
    return macroAutoPortPreferenceFromComboId(macroAutoPortCombo_.getSelectedId());
}

void PreferencesSettingsTab::setMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference pref) {
    macroAutoPortCombo_.setSelectedId(comboIdFromMacroAutoPortPreference(pref), juce::dontSendNotification);
    persistMacroAutoPortPreference(pref);
}

void PreferencesSettingsTab::persistMacroAutoPortPreference(GraphEditor::MacroAutoPortPreference pref) {
    appProperties.getUserSettings()->setValue(kMacroAutoPortPreferenceKey, macroAutoPortPreferenceToString(pref));
    appProperties.getUserSettings()->saveIfNeeded();
    if (graphEditor)
        graphEditor->setMacroAutoPortPreference(pref);
}

const std::vector<juce::String>& PreferencesSettingsTab::getDualIOModuleTypes() { return dualIOModuleTypes(); }

std::map<juce::String, bool> PreferencesSettingsTab::loadDualIOPerModuleOverrides(juce::ApplicationProperties& props) {
    std::map<juce::String, bool> overrides;
    // getValue's default ("{}") is never written back — reading it must not create the key, the
    // same discipline every other "not yet touched" preference in this file follows.
    const auto raw = props.getUserSettings()->getValue(kDualIOPerModuleDefaultsKey, "{}");
    // Held in a named var rather than chained straight into getDynamicObject(): a temporary var's
    // ReferenceCountedObjectPtr releases the DynamicObject the moment the temporary is destroyed
    // (end of this statement), which would leave `obj` dangling for the loop below.
    const juce::var parsed = juce::JSON::parse(raw);
    if (auto* obj = parsed.getDynamicObject()) {
        for (const auto& prop : obj->getProperties())
            overrides[prop.name.toString()] = static_cast<bool>(prop.value);
    }
    return overrides;
}

std::optional<bool> PreferencesSettingsTab::getDualIOOverrideForType(const juce::String& moduleType) const {
    auto it = dualIOPerModuleOverrides.find(moduleType);
    return it != dualIOPerModuleOverrides.end() ? std::optional<bool>(it->second) : std::nullopt;
}

void PreferencesSettingsTab::setDualIOOverrideForType(const juce::String& moduleType,
                                                      std::optional<bool> overrideValue) {
    if (overrideValue.has_value())
        dualIOPerModuleOverrides[moduleType] = *overrideValue;
    else
        dualIOPerModuleOverrides.erase(moduleType);
    persistDualIOPerModuleOverrides();
}

void PreferencesSettingsTab::persistDualIOPerModuleOverrides() {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    for (const auto& [type, dual] : dualIOPerModuleOverrides)
        obj->setProperty(type, dual);
    // Compact (allOnOneLine=true): this is a single ApplicationProperties value, not a file meant
    // to be read by a human.
    appProperties.getUserSettings()->setValue(kDualIOPerModuleDefaultsKey,
                                              juce::JSON::toString(juce::var(obj.get()), true));
    appProperties.getUserSettings()->saveIfNeeded();
    // New-modules-only, exactly like the global default above: no retro-apply to the canvas, and
    // no separate startup-restore step to write here — MainComponent calls
    // loadDualIOPerModuleOverrides() itself and pushes straight into the real GraphEditor, the same
    // way it re-reads "defaultDualIOForNewModules" rather than waiting on this tab to exist.
    if (graphEditor)
        graphEditor->setDualIOPerModuleOverrides(dualIOPerModuleOverrides);
}

std::unique_ptr<juce::Component> PreferencesSettingsTab::buildDualIOPerModuleDefaultsPopup() {
    return std::make_unique<DualIOPerModulePopupContent>(
        getDualIOModuleTypes(), [this](const juce::String& type) { return getDualIOOverrideForType(type); },
        [this](const juce::String& type, std::optional<bool> value) { setDualIOOverrideForType(type, value); });
}

std::unique_ptr<juce::Component> PreferencesSettingsTab::createDualIOPerModuleDefaultsPopupForTest() {
    return buildDualIOPerModuleDefaultsPopup();
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
