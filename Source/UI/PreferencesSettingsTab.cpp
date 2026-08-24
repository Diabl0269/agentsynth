#include "PreferencesSettingsTab.h"
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

// Group-separator alpha. Softened from 0.18: at that contrast the hairlines read as table borders
// and boxed each preference in, which is the same complaint that produced the gentler rule under
// the Keyboard Shortcuts tab's section headers (see its kDividerAlpha — keep the two in step).
constexpr float kDividerAlpha = 0.12f;

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

// Every module type that carries the Dual I/O parameter (ModuleBase::addDualIOParameter): the FX
// modules plus the five split-block voice modules (docs/fx_modules.md § Stereo I/O). Names must
// match ModuleBase::getName() exactly — they are what GraphEditor::addModuleAtCanvasPosition
// receives as the "name" it creates and what applyDefaultDualIOForNewModule matches overrides
// against.
const std::vector<juce::String>& dualIOModuleTypes() {
    static const std::vector<juce::String> types{
        "Oscillator", "Wavetable", "Filter",        "VCA",           "Voice Mixer", "Sampler",
        "Distortion", "Delay",     "Chorus",        "Bitcrusher",    "Limiter",     "Reverb",
        "Flanger",    "Phaser",    "Pitch Shifter", "Parametric EQ", "Compressor",
    };
    return types;
}

// Popup content for the "Per-module I/O defaults..." button: a plain themed column, one
// juce::Label + juce::ComboBox pair per module type, flat children (no per-row wrapper) so tests
// can find the Nth juce::ComboBox the same way ClickingTheToggleReachesTheEditorAndNewModules
// finds the Dual I/O ToggleButton — by walking getChildren() and dynamic_cast. No search box and
// no viewport, unlike MidiDestinationPicker: seventeen rows at kRowHeight fit comfortably inside a
// CallOutBox on any real screen, and a popup this rarely opened does not earn that rig.
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

    dualIOPerModuleOverrides = loadDualIOPerModuleOverrides(appProperties);

    addAndMakeVisible(perModuleDefaultsButton);
    perModuleDefaultsButton.setTooltip(
        "Per-module overrides of the Split Left/Right default above — Follow global, Always on, or Always off for "
        "each module type. Applies to modules created after the change, same as the toggle.");
    perModuleDefaultsButton.onClick = [this] {
        auto popup = buildDualIOPerModuleDefaultsPopup();
        juce::CallOutBox::launchAsynchronously(std::move(popup), perModuleDefaultsButton.getScreenBounds(), nullptr);
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
        "them. Outside the locators — or with no locators set — you still get a one-bar clip. Turn it off to always "
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

    // One line, one row (see the toggle's declaration comment): the button is sized to its own
    // text via changeWidthToFitText rather than a fixed guess, so the toggle keeps as much of the
    // row as it can for its own (longer) label.
    {
        auto dualIORow = bounds.removeFromTop(24);
        perModuleDefaultsButton.changeWidthToFitText(24);
        const int buttonWidth = juce::jmax(perModuleDefaultsButton.getWidth(), 160);
        perModuleDefaultsButton.setBounds(dualIORow.removeFromRight(buttonWidth));
        dualIORow.removeFromRight(12);
        defaultDualIOToggle.setBounds(dualIORow);
    }
    addDivider();

    loopSelectionArmsToggle.setBounds(bounds.removeFromTop(24));
    // Same group as the row above (no divider between them): both are about the loop locators, and
    // separating them would imply they are unrelated settings.
    bounds.removeFromTop(10);
    doubleClickSpansLocatorsToggle.setBounds(bounds.removeFromTop(24));
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
    graphEditor->setDualIOPerModuleOverrides(dualIOPerModuleOverrides);
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
