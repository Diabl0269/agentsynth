// ControlInspectorComponent.cpp -- FRO131 (docs/control/midi-remote-ui.md#inspector-right): see
// the header for scope. `AssignmentRow` is defined here (not in the header, which only
// forward-declares it) because it is held by value-semantics ownership in an
// juce::OwnedArray<AssignmentRow> -- the OwnedArray's deletion needs the complete type, so both
// the class body AND ControlInspectorComponent's own (otherwise-trivial) destructor must live in
// this translation unit, never `= default` back in the header.

#include "UI/MidiRemote/Inspector/ControlInspectorComponent.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <algorithm>

namespace synth::ui {

namespace {

// -- Layout constants shared by ControlInspectorComponent and AssignmentRow --------------------
constexpr int kPadding = 10;
constexpr int kHeaderLineHeight = 20;
constexpr int kHeaderRowHeight = 26;
constexpr int kDividerGap = 14;
constexpr int kAssignmentRowHeight = 132;
constexpr int kLearnTargetRowHeight = 32;
constexpr int kRowSpacing = 10;

// -- Display-string helpers (docs/control/midi-remote.md#data-model) ---------------------------

juce::String controlKindDisplayName(synth::ControlKind kind) {
    switch (kind) {
    case synth::ControlKind::knob:
        return "Knob";
    case synth::ControlKind::fader:
        return "Fader";
    case synth::ControlKind::button:
        return "Button";
    case synth::ControlKind::pad:
        return "Pad";
    case synth::ControlKind::encoder:
        return "Encoder";
    case synth::ControlKind::wheel:
        return "Wheel";
    }
    return "Unknown";
}

juce::String messageTypeDisplayName(synth::MessageType type) {
    switch (type) {
    case synth::MessageType::cc:
        return "CC";
    case synth::MessageType::note:
        return "Note";
    case synth::MessageType::pitchBend:
        return "Pitch Bend";
    case synth::MessageType::channelPressure:
        return "Channel Pressure";
    case synth::MessageType::programChange:
        return "Program Change";
    }
    return "Unknown";
}

// "CC 21 ch 1" / "Pitch Bend ch any" -- see docs/control/midi-remote.md#data-model's MessageSpec.
juce::String formatMessageSpec(const synth::MessageSpec& spec) {
    juce::String text = messageTypeDisplayName(spec.type);
    const bool hasNumber = spec.type == synth::MessageType::cc || spec.type == synth::MessageType::note ||
                           spec.type == synth::MessageType::programChange;
    if (hasNumber)
        text << " " << spec.number;
    text << " ch " << (spec.channel == 0 ? juce::String("any") : juce::String(spec.channel));
    return text;
}

juce::String encodingDisplayName(synth::Encoding encoding) {
    switch (encoding) {
    case synth::Encoding::abs7:
        return "Absolute (7-bit)";
    case synth::Encoding::relTwos:
        return "Relative (Two's Complement)";
    case synth::Encoding::relBinOffset:
        return "Relative (Binary Offset)";
    case synth::Encoding::relSignMag:
        return "Relative (Sign Magnitude)";
    }
    return "Unknown";
}

// ComboBox ids are 1-based (0 means "nothing selected" in JUCE), in the enum's own declaration
// order.
int encodingToComboId(synth::Encoding encoding) { return static_cast<int>(encoding) + 1; }
synth::Encoding comboIdToEncoding(int id) { return static_cast<synth::Encoding>(juce::jmax(1, id) - 1); }

// Same 1-based, declaration-order ids for the kind picker (FRO134 / FRO264).
int kindToComboId(synth::ControlKind kind) { return static_cast<int>(kind) + 1; }
synth::ControlKind comboIdToKind(int id) { return static_cast<synth::ControlKind>(juce::jmax(1, id) - 1); }

void populateKindCombo(juce::ComboBox& combo) {
    for (auto kind : {synth::ControlKind::knob, synth::ControlKind::fader, synth::ControlKind::button,
                      synth::ControlKind::pad, synth::ControlKind::encoder, synth::ControlKind::wheel})
        combo.addItem(controlKindDisplayName(kind), kindToComboId(kind));
}

// Only a continuous CC control can be an encoder whose encoding is worth detecting.
bool canAutoDetectEncoding(const synth::Control& control) {
    return control.message.type == synth::MessageType::cc &&
           (control.kind == synth::ControlKind::knob || control.kind == synth::ControlKind::encoder);
}

// -- Takeover combo (docs/control/midi-remote.md#takeover): "Default/Jump/Pick-up/Scale" ---------
int takeoverToComboId(synth::Takeover takeover) {
    switch (takeover) {
    case synth::Takeover::useDefault:
        return 1;
    case synth::Takeover::jump:
        return 2;
    case synth::Takeover::pickup:
        return 3;
    case synth::Takeover::scale:
        return 4;
    }
    return 1;
}

synth::Takeover comboIdToTakeover(int id) {
    switch (id) {
    case 2:
        return synth::Takeover::jump;
    case 3:
        return synth::Takeover::pickup;
    case 4:
        return synth::Takeover::scale;
    case 1:
    default:
        return synth::Takeover::useDefault;
    }
}

void populateEncodingCombo(juce::ComboBox& combo) {
    combo.addItem(encodingDisplayName(synth::Encoding::abs7), encodingToComboId(synth::Encoding::abs7));
    combo.addItem(encodingDisplayName(synth::Encoding::relTwos), encodingToComboId(synth::Encoding::relTwos));
    combo.addItem(encodingDisplayName(synth::Encoding::relBinOffset), encodingToComboId(synth::Encoding::relBinOffset));
    combo.addItem(encodingDisplayName(synth::Encoding::relSignMag), encodingToComboId(synth::Encoding::relSignMag));
}

// "Default (<the Preferences default>)": the row names what Default currently means (FRO136).
juce::String defaultTakeoverItemText(synth::Takeover preferencesDefault) {
    switch (preferencesDefault) {
    case synth::Takeover::jump:
        return "Default (Jump)";
    case synth::Takeover::pickup:
        return "Default (Pick-up)";
    default:
        return "Default (Scale)";
    }
}

void populateTakeoverCombo(juce::ComboBox& combo, synth::Takeover preferencesDefault) {
    combo.addItem(defaultTakeoverItemText(preferencesDefault), takeoverToComboId(synth::Takeover::useDefault));
    combo.addItem("Jump", takeoverToComboId(synth::Takeover::jump));
    combo.addItem("Pick-up", takeoverToComboId(synth::Takeover::pickup));
    combo.addItem("Scale", takeoverToComboId(synth::Takeover::scale));
}

} // namespace

// ================================================================================================
// AssignmentRow -- one block under the divider for one Assignment (docs/control/midi-remote-ui.md
// #inspector-right). Fully owned/defined here; ControlInspectorComponent only forward-declares it.
// ================================================================================================
class ControlInspectorComponent::AssignmentRow : public juce::Component {
public:
    AssignmentRow(const AssignmentRowModel& rowModel, int index, synth::Takeover preferencesDefault)
        : model_(rowModel) {
        setComponentID("assignmentRow" + juce::String(index));

        scopeLabel_.setText(model_.scopeLabel, juce::dontSendNotification);
        scopeLabel_.setComponentID("assignmentScopeLabel" + juce::String(index));
        scopeLabel_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        addAndMakeVisible(scopeLabel_);

        drivesLabel_.setText(model_.drivesLabel, juce::dontSendNotification);
        drivesLabel_.setComponentID("assignmentDrivesLabel" + juce::String(index));
        const bool clickable = model_.nodeUuid.isNotEmpty();
        if (clickable) {
            drivesLabel_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
            drivesLabel_.onClicked = [this] {
                if (onLocateRequested)
                    onLocateRequested(model_.nodeUuid);
            };
        }
        addAndMakeVisible(drivesLabel_);

        populateTakeoverCombo(takeoverCombo_, preferencesDefault);
        takeoverCombo_.setComponentID("assignmentTakeoverCombo" + juce::String(index));
        takeoverCombo_.onChange = [this] { commitTakeover(); };
        addAndMakeVisible(takeoverCombo_);

        rangeMinEditor_.setComponentID("assignmentRangeMinEditor" + juce::String(index));
        rangeMinEditor_.setInputRestrictions(4, "0123456789");
        rangeMinEditor_.setJustification(juce::Justification::centred);
        rangeMinEditor_.onReturnKey = [this] { commitRange(); };
        rangeMinEditor_.onFocusLost = [this] { commitRange(); };
        addAndMakeVisible(rangeMinEditor_);

        rangeMaxEditor_.setComponentID("assignmentRangeMaxEditor" + juce::String(index));
        rangeMaxEditor_.setInputRestrictions(4, "0123456789");
        rangeMaxEditor_.setJustification(juce::Justification::centred);
        rangeMaxEditor_.onReturnKey = [this] { commitRange(); };
        rangeMaxEditor_.onFocusLost = [this] { commitRange(); };
        addAndMakeVisible(rangeMaxEditor_);

        rangeSeparatorLabel_.setText("-", juce::dontSendNotification);
        rangeSeparatorLabel_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(rangeSeparatorLabel_);

        invertToggle_.setButtonText("Invert");
        invertToggle_.setComponentID("assignmentInvertToggle" + juce::String(index));
        invertToggle_.onClick = [this] { toggleInvert(); };
        addAndMakeVisible(invertToggle_);

        forgetButton_.setButtonText("Forget");
        forgetButton_.setComponentID("assignmentForgetButton" + juce::String(index));
        forgetButton_.onClick = [this] {
            if (onForgetRequested)
                onForgetRequested(model_.assignment.id);
        };
        addAndMakeVisible(forgetButton_);

        refreshFromModel();
    }

    std::function<void(const juce::String&)> onLocateRequested;
    std::function<void(const synth::Assignment&)> onAssignmentEdited;
    std::function<void(const juce::String&)> onForgetRequested;

    void resized() override {
        auto bounds = getLocalBounds();

        auto headerRow = bounds.removeFromTop(kHeaderLineHeight);
        scopeLabel_.setBounds(headerRow.removeFromLeft(70));
        drivesLabel_.setBounds(headerRow);
        bounds.removeFromTop(6);

        auto takeoverRow = bounds.removeFromTop(kHeaderRowHeight);
        takeoverCombo_.setBounds(takeoverRow.removeFromLeft(160));
        bounds.removeFromTop(6);

        auto rangeRow = bounds.removeFromTop(kHeaderRowHeight);
        rangeMinEditor_.setBounds(rangeRow.removeFromLeft(56));
        rangeSeparatorLabel_.setBounds(rangeRow.removeFromLeft(16));
        rangeMaxEditor_.setBounds(rangeRow.removeFromLeft(56));
        rangeRow.removeFromLeft(10);
        invertToggle_.setBounds(rangeRow.removeFromLeft(80));
        bounds.removeFromTop(6);

        auto footerRow = bounds.removeFromTop(kHeaderRowHeight);
        forgetButton_.setBounds(footerRow.removeFromRight(80));
    }

private:
    // A juce::Label that fires onClicked on a real mouse click, used for "Drives" -- clicking
    // jumps to the module on the canvas (docs/control/midi-remote-ui.md#inspector-right). Only
    // wired (by the owning row) when the target is a parameter -- an action target has no node to
    // locate.
    class ClickableLabel : public juce::Label {
    public:
        std::function<void()> onClicked;

        // Public, matching MixerColumnComponent/ControllersListComponent's own convention, so a
        // test can drive a real click through this exactly like a genuine one would. Not gated on
        // juce::MouseEvent::mouseWasClicked() -- same call as MixerColumnComponent::mouseUp (see
        // its own comment): that flag reads real MouseInputSource drag-distance/time state a
        // synthesized event never populates the same way a live click does, so a headless
        // "press and release" test never satisfies it.
        void mouseUp(const juce::MouseEvent& e) override {
            juce::Label::mouseUp(e);
            if (onClicked)
                onClicked();
        }
    };

    // Applies `model_.assignment.target.kind == parameter` -- the only target kind takeover/range
    // apply to (docs/control/midi-remote.md#takeover: "not buttons", and an action or nodeCommand
    // target is always button-like, docs/control/midi-remote.md#node-command-targets).
    // An orphaned row (its target no longer resolves) offers Forget only.
    bool isTakeoverEditable() const {
        return model_.assignment.target.kind == synth::Target::Kind::parameter && !model_.isOrphaned;
    }

    // Same dynamic_cast-with-null-fallback convention MixerDockComponent::refreshDetachButton()
    // uses (Source/UI/Mixer/MixerDockComponent.cpp) -- resolved once here rather than in paint(),
    // since only isOrphaned (fixed for the row's lifetime, set on rebuild) drives it.
    void refreshFromModel() {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        scopeLabel_.setColour(juce::Label::textColourId,
                              lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::grey);
        const juce::Colour warningColour = lf != nullptr ? lf->getTheme().colors.warning : juce::Colours::orange;
        const juce::Colour normalColour = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
        drivesLabel_.setColour(juce::Label::textColourId, model_.isOrphaned ? warningColour : normalColour);

        takeoverCombo_.setSelectedId(takeoverToComboId(model_.assignment.takeover), juce::dontSendNotification);
        rangeMinEditor_.setText(juce::String(juce::roundToInt(model_.assignment.range.min * 100.0)),
                                juce::dontSendNotification);
        rangeMaxEditor_.setText(juce::String(juce::roundToInt(model_.assignment.range.max * 100.0)),
                                juce::dontSendNotification);
        invertToggle_.setToggleState(model_.assignment.range.min > model_.assignment.range.max,
                                     juce::dontSendNotification);

        const bool editable = isTakeoverEditable();
        takeoverCombo_.setEnabled(editable);
        rangeMinEditor_.setEnabled(editable);
        rangeMaxEditor_.setEnabled(editable);
        invertToggle_.setEnabled(editable);
        const juce::String reason = model_.isOrphaned
                                        ? juce::String("This target is missing - Forget it, or map the control again")
                                        : juce::String("Takeover and range apply only to a parameter target's absolute "
                                                       "continuous encoding, not a button-like action");
        takeoverCombo_.setTooltip(editable ? juce::String() : reason);
        rangeMinEditor_.setTooltip(editable ? juce::String() : reason);
        rangeMaxEditor_.setTooltip(editable ? juce::String() : reason);
        invertToggle_.setTooltip(editable ? juce::String() : reason);
    }

    void commitTakeover() {
        auto updated = model_.assignment;
        updated.takeover = comboIdToTakeover(takeoverCombo_.getSelectedId());
        model_.assignment = updated;
        if (onAssignmentEdited)
            onAssignmentEdited(updated);
    }

    void commitRange() {
        auto updated = model_.assignment;
        updated.range.min = juce::jlimit(0.0, 1.0, rangeMinEditor_.getText().getDoubleValue() / 100.0);
        updated.range.max = juce::jlimit(0.0, 1.0, rangeMaxEditor_.getText().getDoubleValue() / 100.0);
        model_.assignment = updated;
        invertToggle_.setToggleState(updated.range.min > updated.range.max, juce::dontSendNotification);
        if (onAssignmentEdited)
            onAssignmentEdited(updated);
    }

    // Checkbox reflects/sets range.min > range.max (docs/control/midi-remote.md#data-model's
    // "min > max inverts"). Computed from the model's own range, never from the checkbox's own
    // getToggleState() -- calling onClick() directly (this codebase's established substitute for a
    // real Button click headlessly, e.g. TimelinePanelZoomSnapScrollTests.cpp's
    // getSnapToggleButton().onClick()) does not run JUCE's own toggle-flip first, so the widget's
    // own state can't be trusted as "already flipped" the way a live click leaves it.
    void toggleInvert() {
        auto updated = model_.assignment;
        const bool invertOn = !(updated.range.min > updated.range.max);
        const double lo = std::min(updated.range.min, updated.range.max);
        const double hi = std::max(updated.range.min, updated.range.max);
        updated.range.min = invertOn ? hi : lo;
        updated.range.max = invertOn ? lo : hi;
        model_.assignment = updated;
        rangeMinEditor_.setText(juce::String(juce::roundToInt(updated.range.min * 100.0)), juce::dontSendNotification);
        rangeMaxEditor_.setText(juce::String(juce::roundToInt(updated.range.max * 100.0)), juce::dontSendNotification);
        invertToggle_.setToggleState(invertOn, juce::dontSendNotification);
        if (onAssignmentEdited)
            onAssignmentEdited(updated);
    }

    AssignmentRowModel model_;
    juce::Label scopeLabel_;
    ClickableLabel drivesLabel_;
    juce::ComboBox takeoverCombo_;
    juce::TextEditor rangeMinEditor_;
    juce::Label rangeSeparatorLabel_;
    juce::TextEditor rangeMaxEditor_;
    juce::ToggleButton invertToggle_;
    juce::TextButton forgetButton_;
};

// ================================================================================================
// ControlInspectorComponent
// ================================================================================================

ControlInspectorComponent::ControlInspectorComponent() {
    nameLabel_.setComponentID("controlNameLabel");
    nameLabel_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    nameLabel_.setEditable(false, true, false); // FRO134: double-click to rename
    nameLabel_.onTextChange = [this] {
        const auto trimmed = nameLabel_.getText().trim();
        if (!model_.hasControl || trimmed.isEmpty() || trimmed == model_.control.name) {
            nameLabel_.setText(model_.control.name, juce::dontSendNotification); // revert an empty/no-op edit
            return;
        }
        model_.control.name = trimmed;
        fireControlEdited();
    };
    addAndMakeVisible(nameLabel_);

    populateKindCombo(kindCombo_);
    kindCombo_.setComponentID("controlKindCombo");
    kindCombo_.onChange = [this] {
        if (!model_.hasControl)
            return;
        const auto kind = comboIdToKind(kindCombo_.getSelectedId());
        if (kind == model_.control.kind)
            return;
        model_.control.kind = kind;
        fireControlEdited();
    };
    addAndMakeVisible(kindCombo_);

    messageSpecLabel_.setComponentID("controlMessageSpecLabel");
    addAndMakeVisible(messageSpecLabel_);

    // Relearn stays rendered but INERT (see the header's own comment).
    relearnButton_.setComponentID("relearnButton");
    relearnButton_.setEnabled(false);
    relearnButton_.setTooltip("Coming in a later update");
    addAndMakeVisible(relearnButton_);

    populateEncodingCombo(encodingCombo_);
    encodingCombo_.setComponentID("encodingCombo");
    encodingCombo_.onChange = [this] {
        if (!model_.hasControl)
            return;
        const auto encoding = comboIdToEncoding(encodingCombo_.getSelectedId());
        if (encoding == model_.control.encoding)
            return;
        model_.control.encoding = encoding;
        fireControlEdited();
    };
    addAndMakeVisible(encodingCombo_);

    autoDetectButton_.setComponentID("autoDetectButton");
    autoDetectButton_.setTooltip("Turn the control left, then right, to detect how it encodes");
    autoDetectButton_.onClick = [this] {
        if (model_.hasControl && onAutoDetectRequested)
            onAutoDetectRequested(model_.control);
    };
    addAndMakeVisible(autoDetectButton_);

    buttonModeLabel_.setComponentID("buttonModeLabel");
    addAndMakeVisible(buttonModeLabel_);

    learnTargetButton_.setComponentID("learnTargetButton");
    learnTargetButton_.setTooltip("Choose what this control drives: a control on the canvas, or an action");
    learnTargetButton_.onClick = [this] {
        if (model_.hasControl && onLearnTargetRequested)
            onLearnTargetRequested(learnTargetButton_);
    };
    addAndMakeVisible(learnTargetButton_);
}

// Out-of-line so the OwnedArray<AssignmentRow> member can delete its (complete, here) element
// type -- see this file's header comment.
ControlInspectorComponent::~ControlInspectorComponent() = default;

void ControlInspectorComponent::setControl(const ControlModel& model) {
    model_ = model;
    nameLabel_.setEditable(false, model_.hasControl, false);
    rebuildRows();

    if (!model_.hasControl) {
        nameLabel_.setText("No control selected", juce::dontSendNotification);
        nameLabel_.setVisible(true);
        kindCombo_.setVisible(false);
        messageSpecLabel_.setVisible(false);
        relearnButton_.setVisible(false);
        encodingCombo_.setVisible(false);
        autoDetectButton_.setVisible(false);
        buttonModeLabel_.setVisible(false);
        learnTargetButton_.setVisible(false);
        resized();
        repaint();
        return;
    }

    nameLabel_.setText(model_.control.name, juce::dontSendNotification);
    nameLabel_.setVisible(true);

    kindCombo_.setSelectedId(kindToComboId(model_.control.kind), juce::dontSendNotification);
    kindCombo_.setVisible(true);

    messageSpecLabel_.setText(formatMessageSpec(model_.control.message), juce::dontSendNotification);
    messageSpecLabel_.setVisible(true);

    relearnButton_.setVisible(true);

    encodingCombo_.setSelectedId(encodingToComboId(model_.control.encoding), juce::dontSendNotification);
    encodingCombo_.setVisible(true);

    autoDetectButton_.setVisible(true);
    autoDetectButton_.setEnabled(canAutoDetectEncoding(model_.control));

    buttonModeLabel_.setText(model_.control.buttonMode == synth::ButtonMode::toggle ? "Toggle" : "Momentary",
                             juce::dontSendNotification);
    buttonModeLabel_.setVisible(true);
    learnTargetButton_.setVisible(true);

    resized();
    repaint();
}

void ControlInspectorComponent::setDefaultTakeover(synth::Takeover takeover) {
    if (takeover == synth::Takeover::useDefault || takeover == defaultTakeover_)
        return;
    defaultTakeover_ = takeover;
    setControl(model_); // the rows' Default item carries the name
}

void ControlInspectorComponent::fireControlEdited() {
    if (onControlEdited)
        onControlEdited(model_.control);
}

void ControlInspectorComponent::rebuildRows() {
    assignmentRows_.clear();
    if (!model_.hasControl)
        return;

    int index = 0;
    for (const auto& rowModel : model_.assignments) {
        auto* row = assignmentRows_.add(new AssignmentRow(rowModel, index++, defaultTakeover_));
        row->onLocateRequested = [this](const juce::String& nodeUuid) {
            if (onLocateRequested)
                onLocateRequested(nodeUuid);
        };
        row->onAssignmentEdited = [this](const synth::Assignment& assignment) {
            if (onAssignmentEdited)
                onAssignmentEdited(assignment);
        };
        row->onForgetRequested = [this](const juce::String& assignmentId) {
            if (onForgetRequested)
                onForgetRequested(assignmentId);
        };
        addAndMakeVisible(row);
    }
}

void ControlInspectorComponent::resized() {
    auto bounds = getLocalBounds().reduced(kPadding);

    nameLabel_.setBounds(bounds.removeFromTop(24));
    if (!model_.hasControl)
        return;

    kindCombo_.setBounds(bounds.removeFromTop(kHeaderRowHeight).removeFromLeft(180));
    messageSpecLabel_.setBounds(bounds.removeFromTop(kHeaderLineHeight));

    auto relearnRow = bounds.removeFromTop(kHeaderRowHeight);
    relearnButton_.setBounds(relearnRow.removeFromLeft(90));

    auto encodingRow = bounds.removeFromTop(kHeaderRowHeight);
    encodingCombo_.setBounds(encodingRow.removeFromLeft(180));
    encodingRow.removeFromLeft(8);
    autoDetectButton_.setBounds(encodingRow.removeFromLeft(110));

    buttonModeLabel_.setBounds(bounds.removeFromTop(kHeaderLineHeight));

    bounds.removeFromTop(kDividerGap);
    learnTargetButton_.setBounds(bounds.removeFromTop(kLearnTargetRowHeight).removeFromLeft(120).reduced(0, 3));
    for (auto* row : assignmentRows_) {
        row->setBounds(bounds.removeFromTop(kAssignmentRowHeight));
        bounds.removeFromTop(kRowSpacing);
    }
}

void ControlInspectorComponent::paint(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour background = lf != nullptr ? lf->getTheme().colors.surface : juce::Colours::black;
    g.fillAll(background);

    if (!model_.hasControl)
        return;

    const juce::Colour lineColour = lf != nullptr ? lf->getTheme().colors.border : juce::Colours::grey;
    g.setColour(lineColour);
    const int dividerY = buttonModeLabel_.getBottom() + kDividerGap / 2;
    g.drawHorizontalLine(dividerY, static_cast<float>(getLocalBounds().getX()),
                         static_cast<float>(getLocalBounds().getRight()));
}

} // namespace synth::ui
