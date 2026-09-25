#include "AI/AIStateMapper/AIStateMapper.h"
#include "PreferencesSettingsTab.h"
#include "PreferencesSettingsTabInternal.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>

// Concern: the editor/canvas-behaviour preferences — smart connections, double-click
// disconnect, alignment guides, Dual I/O (global + per-module popup), the macro auto-port
// preference and its T148 toggles, and the T184 mixer auto-create-channel toggle.

namespace {
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
} // namespace

namespace {
// Every module type that carries the Dual I/O parameter — which ModuleBase's constructor grants from
// the module's channel shape (ModuleBase::StereoAudio): the FX modules plus the split-block voice
// modules (docs/modules/fx-modules.md#stereo-io-dual-io-toggle).
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
} // namespace

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

bool PreferencesSettingsTab::isMacroAutoCreatePortsOnDragEnabled() const {
    return macroAutoCreatePortsOnDragToggle.getToggleState();
}

void PreferencesSettingsTab::setMacroAutoCreatePortsOnDragEnabled(bool enabled) {
    macroAutoCreatePortsOnDragToggle.setToggleState(enabled, juce::dontSendNotification);
    persistMacroAutoCreatePortsOnDrag(enabled);
}

bool PreferencesSettingsTab::isMacroAutoDeletePortsOnLastCableEnabled() const {
    return macroAutoDeletePortsOnLastCableToggle.getToggleState();
}

void PreferencesSettingsTab::setMacroAutoDeletePortsOnLastCableEnabled(bool enabled) {
    macroAutoDeletePortsOnLastCableToggle.setToggleState(enabled, juce::dontSendNotification);
    persistMacroAutoDeletePortsOnLastCable(enabled);
}

bool PreferencesSettingsTab::isMacroDragWithoutCmdEnabled() const { return macroDragWithoutCmdToggle.getToggleState(); }

void PreferencesSettingsTab::setMacroDragWithoutCmdEnabled(bool enabled) {
    macroDragWithoutCmdToggle.setToggleState(enabled, juce::dontSendNotification);
    persistMacroDragWithoutCmd(enabled);
}

bool PreferencesSettingsTab::isMixerAutoCreateChannelOnConnectEnabled() const {
    return mixerAutoCreateChannelOnConnectToggle.getToggleState();
}

void PreferencesSettingsTab::setMixerAutoCreateChannelOnConnectEnabled(bool enabled) {
    mixerAutoCreateChannelOnConnectToggle.setToggleState(enabled, juce::dontSendNotification);
    persistMixerAutoCreateChannelOnConnect(enabled);
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

void PreferencesSettingsTab::persistSmartConnectionMode(GraphEditor::SmartConnectionMode mode) {
    appProperties.getUserSettings()->setValue("smartConnectionMode", GraphEditor::smartConnectionModeToString(mode));
    appProperties.getUserSettings()->saveIfNeeded();
    if (graphEditor)
        graphEditor->getSmartConnections().setSmartConnectionMode(mode);
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

void PreferencesSettingsTab::persistMacroAutoCreatePortsOnDrag(bool enabled) {
    appProperties.getUserSettings()->setValue("macroAutoCreatePortsOnDrag", enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    if (graphEditor)
        graphEditor->setAutoCreateMacroPortsOnDragEnabled(enabled);
}

void PreferencesSettingsTab::persistMacroAutoDeletePortsOnLastCable(bool enabled) {
    appProperties.getUserSettings()->setValue("macroAutoDeletePortsOnLastCable", enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    if (graphEditor)
        graphEditor->setAutoDeleteMacroPortsOnLastCableEnabled(enabled);
}

void PreferencesSettingsTab::persistMacroDragWithoutCmd(bool enabled) {
    appProperties.getUserSettings()->setValue("macroDragWithoutCmd", enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    if (graphEditor)
        graphEditor->setMacroDragWithoutCmdEnabled(enabled);
}

void PreferencesSettingsTab::persistMixerAutoCreateChannelOnConnect(bool enabled) {
    appProperties.getUserSettings()->setValue("mixerAutoCreateChannelOnConnect", enabled ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    if (graphEditor)
        graphEditor->setAutoCreateChannelOnConnectEnabled(enabled);
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

GraphEditor::MacroAutoPortPreference
PreferencesSettingsTab::loadMacroAutoPortPreference(juce::ApplicationProperties& props) {
    // Reading with the "ask" default must not create the key, matching every other "not yet
    // touched" preference's discipline in this file; macroAutoPortPreferenceFromString treats any
    // unknown value as Unset, so a stale/garbage entry degrades to the safe asking default too.
    return macroAutoPortPreferenceFromString(props.getUserSettings()->getValue(kMacroAutoPortPreferenceKey, "ask"));
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

// Builds the macro on/off toggles (T148 auto-create/auto-delete, FRO168 drag without Cmd). Pulled
// out of the constructor, which is on the function-size ratchet, into its own named step.
void PreferencesSettingsTab::initMacroToggles() {
    // T148 (docs/macros/auto-ports.md#ports-on-a-cable-drag): auto-create/auto-delete are plain on/off, unlike the
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

    // FRO168: plain on/off, ON by default (Cmd-drag reparents either way).
    contentHost.addAndMakeVisible(macroDragWithoutCmdToggle);
    macroDragWithoutCmdToggle.setToggleState(appProperties.getUserSettings()->getBoolValue("macroDragWithoutCmd", true),
                                             juce::dontSendNotification);
    macroDragWithoutCmdToggle.setTooltip(
        "When on (the default), dragging a single module across an expanded macro's border adds it to "
        "or removes it from that macro without holding Cmd, and dropping a module from the library "
        "onto a macro adds it there. Holding Cmd does the same either way. Group drags and Ctrl-drags "
        "never change membership. With a small macro, plainly rearranging a member can read as "
        "leaving it; turn this off to require Cmd.");
    macroDragWithoutCmdToggle.onClick = [this] {
        persistMacroDragWithoutCmd(macroDragWithoutCmdToggle.getToggleState());
    };
}

// Lays out the macro toggle group; returns whether it is visible under the current search filter.
// `groupMatches`/`setGroupVisible`/`beginGroup` are layoutContent's own search-filter helpers,
// forwarded through rather than duplicated (same shape as layoutMixerDefaultTrackPresetGroup).
bool PreferencesSettingsTab::layoutMacroToggleGroup(
    int& y, int contentWidth, const std::function<bool(std::initializer_list<juce::Component*>)>& groupMatches,
    const std::function<void(std::initializer_list<juce::Component*>, bool)>& setGroupVisible,
    const std::function<void(bool)>& beginGroup) {
    const std::initializer_list<juce::Component*> comps = {
        &macroAutoCreatePortsOnDragToggle, &macroAutoDeletePortsOnLastCableToggle, &macroDragWithoutCmdToggle};
    const bool visible = groupMatches(comps);
    setGroupVisible(comps, visible);
    beginGroup(visible);
    if (visible) {
        for (auto* toggle : comps) {
            toggle->setBounds({0, y, contentWidth, 24});
            y += 24;
        }
    }
    return visible;
}
