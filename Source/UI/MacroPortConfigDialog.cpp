#include "MacroPortConfigDialog.h"

namespace synth::ui {

namespace {
// ComboBox item ids are 1-based in JUCE.
constexpr int kDirectionInputId = 1;
constexpr int kDirectionOutputId = 2;

constexpr int kKindAudioCVId = 1;
constexpr int kKindMidiId = 2;

constexpr int kShapeMonoId = 1;
constexpr int kShapeStereoId = 2;
constexpr int kShapePolyId = 3;
} // namespace

void MacroPortConfigDialog::populateShapeBox(juce::ComboBox& box) {
    box.addItem("Mono", kShapeMonoId);
    box.addItem("Stereo", kShapeStereoId);
    box.addItem("Poly-N", kShapePolyId);
}

MacroPortShape MacroPortConfigDialog::shapeFromComboIndex(int itemId) {
    if (itemId == kShapeStereoId)
        return MacroPortShape::Stereo;
    if (itemId == kShapePolyId)
        return MacroPortShape::Poly;
    return MacroPortShape::Mono;
}

int MacroPortConfigDialog::comboIndexFromShape(MacroPortShape shape) {
    switch (shape) {
    case MacroPortShape::Stereo:
        return kShapeStereoId;
    case MacroPortShape::Poly:
        return kShapePolyId;
    case MacroPortShape::Mono:
    default:
        return kShapeMonoId;
    }
}

MacroPortConfigDialog::MacroPortConfigDialog(juce::String macroName, std::vector<PortRow> ports)
    : macroName_(std::move(macroName))
    , rows_(std::move(ports)) {
    titleLabel_.setText("Configure I/O - " + macroName_, juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel_);

    addAndMakeVisible(newPortSectionLabel_);

    newDirectionBox_.addItem("Input", kDirectionInputId);
    newDirectionBox_.addItem("Output", kDirectionOutputId);
    newDirectionBox_.setSelectedId(kDirectionInputId, juce::dontSendNotification);
    addAndMakeVisible(newDirectionBox_);

    newKindBox_.addItem("Audio / CV", kKindAudioCVId);
    newKindBox_.addItem("MIDI", kKindMidiId);
    newKindBox_.setSelectedId(kKindAudioCVId, juce::dontSendNotification);
    newKindBox_.onChange = [this] {
        const bool isMidi = newKindBox_.getSelectedId() == kKindMidiId;
        newShapeBox_.setEnabled(!isMidi);
        newVoicesEditor_.setEnabled(!isMidi && newShapeBox_.getSelectedId() == kShapePolyId);
    };
    addAndMakeVisible(newKindBox_);

    populateShapeBox(newShapeBox_);
    newShapeBox_.setSelectedId(kShapeMonoId, juce::dontSendNotification);
    newShapeBox_.onChange = [this] {
        newVoicesEditor_.setEnabled(newKindBox_.getSelectedId() == kKindAudioCVId &&
                                    newShapeBox_.getSelectedId() == kShapePolyId);
    };
    addAndMakeVisible(newShapeBox_);

    newVoicesEditor_.setText("4", juce::dontSendNotification);
    newVoicesEditor_.setInputRestrictions(2, "0123456789");
    newVoicesEditor_.setEnabled(false); // Mono is the default shape
    addAndMakeVisible(newVoicesEditor_);

    newNameEditor_.setTextToShowWhenEmpty("Port name", juce::Colours::grey);
    addAndMakeVisible(newNameEditor_);

    addButton_.onClick = [this] { triggerAddPortForTest(); };
    addAndMakeVisible(addButton_);

    closeButton_.onClick = [this] {
        if (onRequestClose)
            onRequestClose();
    };
    addAndMakeVisible(closeButton_);

    rebuildRowComponents();
    setSize(kDialogWidth, contentHeightForCurrentRows());
}

MacroPortConfigDialog::~MacroPortConfigDialog() = default;

void MacroPortConfigDialog::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    // One rule under the "add a port" section, one above the row list — cheap visual grouping,
    // no theming dependency (this dialog is content-only, hosted in a plain juce::DialogWindow).
    const int ruleY1 = newPortSectionLabel_.getBottom() + kNewPortRowHeight * 2 + 4;
    g.drawHorizontalLine(ruleY1, (float)kMargin, (float)(getWidth() - kMargin));
}

void MacroPortConfigDialog::resized() {
    auto area = getLocalBounds().reduced(kMargin);

    titleLabel_.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);

    newPortSectionLabel_.setBounds(area.removeFromTop(20));

    auto newRow1 = area.removeFromTop(kNewPortRowHeight);
    newDirectionBox_.setBounds(newRow1.removeFromLeft(110));
    newRow1.removeFromLeft(6);
    newKindBox_.setBounds(newRow1.removeFromLeft(110));
    newRow1.removeFromLeft(6);
    newShapeBox_.setBounds(newRow1.removeFromLeft(90));
    newRow1.removeFromLeft(6);
    newVoicesEditor_.setBounds(newRow1.removeFromLeft(50));

    area.removeFromTop(4);
    auto newRow2 = area.removeFromTop(kNewPortRowHeight);
    addButton_.setBounds(newRow2.removeFromRight(80));
    newRow2.removeFromRight(6);
    newNameEditor_.setBounds(newRow2);

    area.removeFromTop(12); // room for the rule paint() draws just above the row list

    for (auto* rc : rowControls_) {
        auto row = area.removeFromTop(kRowHeight);
        rc->directionLabel.setBounds(row.removeFromLeft(40));
        row.removeFromLeft(4);
        rc->deleteButton.setBounds(row.removeFromRight(60));
        row.removeFromRight(4);
        rc->downButton.setBounds(row.removeFromRight(50));
        row.removeFromRight(4);
        rc->upButton.setBounds(row.removeFromRight(40));
        row.removeFromRight(4);
        rc->applyShapeButton.setBounds(row.removeFromRight(90));
        row.removeFromRight(4);
        rc->voicesEditor.setBounds(row.removeFromRight(50));
        row.removeFromRight(4);
        rc->shapeBox.setBounds(row.removeFromRight(90));
        row.removeFromRight(4);
        rc->kindLabel.setBounds(row.removeFromRight(70));
        row.removeFromRight(4);
        rc->nameEditor.setBounds(row);
        area.removeFromTop(2);
    }

    area.removeFromTop(8);
    closeButton_.setBounds(area.removeFromTop(kNewPortRowHeight).removeFromRight(90));
}

int MacroPortConfigDialog::contentHeightForCurrentRows() const {
    const int rowsHeight = (int)rowControls_.size() * (kRowHeight + 2);
    return 28 + 8 + 20 + kNewPortRowHeight + 4 + kNewPortRowHeight + 12 + rowsHeight + 8 + kNewPortRowHeight +
           kMargin * 2;
}

void MacroPortConfigDialog::updateVoicesEnablement(RowControls& rc) {
    rc.voicesEditor.setEnabled(rc.shapeBox.getSelectedId() == kShapePolyId);
}

void MacroPortConfigDialog::rebuildRowComponents() {
    rowControls_.clear();

    for (const auto& row : rows_) {
        auto* rc = new RowControls();
        rowControls_.add(rc);

        rc->directionLabel.setText(row.isInput ? "In" : "Out", juce::dontSendNotification);
        addAndMakeVisible(rc->directionLabel);

        rc->nameEditor.setText(row.name, juce::dontSendNotification);
        const juce::String uuid = row.nodeUuid;
        rc->nameEditor.onFocusLost = [this, uuid, rc] {
            if (onRenamePort)
                onRenamePort(uuid, rc->nameEditor.getText());
        };
        rc->nameEditor.onReturnKey = [this, uuid, rc] {
            if (onRenamePort)
                onRenamePort(uuid, rc->nameEditor.getText());
        };
        addAndMakeVisible(rc->nameEditor);

        const bool isMidi = row.kind == synth::MacroPortKind::Midi;
        rc->kindLabel.setText(isMidi ? "MIDI" : "Audio / CV", juce::dontSendNotification);
        addAndMakeVisible(rc->kindLabel);

        populateShapeBox(rc->shapeBox);
        rc->shapeBox.setSelectedId(comboIndexFromShape(row.shape), juce::dontSendNotification);
        rc->shapeBox.setEnabled(!isMidi);
        rc->shapeBox.onChange = [this, rc] { updateVoicesEnablement(*rc); };
        addAndMakeVisible(rc->shapeBox);

        rc->voicesEditor.setText(juce::String(row.voiceCount), juce::dontSendNotification);
        rc->voicesEditor.setInputRestrictions(2, "0123456789");
        rc->voicesEditor.setEnabled(!isMidi && row.shape == MacroPortShape::Poly);
        addAndMakeVisible(rc->voicesEditor);

        rc->applyShapeButton.setEnabled(!isMidi); // a MIDI port has no shape to change (§5.1)
        rc->applyShapeButton.onClick = [this, uuid, rc] {
            if (!onChangePortShape)
                return;
            const auto shape = shapeFromComboIndex(rc->shapeBox.getSelectedId());
            const int voices = juce::jmax(1, rc->voicesEditor.getText().getIntValue());
            onChangePortShape(uuid, shape, voices);
        };
        addAndMakeVisible(rc->applyShapeButton);

        rc->upButton.onClick = [this, uuid] {
            if (onReorderPort)
                onReorderPort(uuid, /*moveUp=*/true);
        };
        addAndMakeVisible(rc->upButton);

        rc->downButton.onClick = [this, uuid] {
            if (onReorderPort)
                onReorderPort(uuid, /*moveUp=*/false);
        };
        addAndMakeVisible(rc->downButton);

        rc->deleteButton.onClick = [this, uuid] {
            if (onDeletePort)
                onDeletePort(uuid);
        };
        addAndMakeVisible(rc->deleteButton);
    }
}

void MacroPortConfigDialog::refreshPorts(std::vector<PortRow> ports) {
    rows_ = std::move(ports);
    rebuildRowComponents();
    setSize(kDialogWidth, contentHeightForCurrentRows());
    resized();
    repaint();
}

// ---- Test seams -------------------------------------------------------------------------------

juce::String MacroPortConfigDialog::getRowNodeUuidForTest(int row) const {
    return (row >= 0 && row < (int)rows_.size()) ? rows_[(size_t)row].nodeUuid : juce::String();
}

juce::String MacroPortConfigDialog::getRowNameForTest(int row) const {
    return (row >= 0 && row < (int)rowControls_.size()) ? rowControls_[row]->nameEditor.getText() : juce::String();
}

bool MacroPortConfigDialog::getRowIsInputForTest(int row) const {
    return (row >= 0 && row < (int)rows_.size()) && rows_[(size_t)row].isInput;
}

void MacroPortConfigDialog::setNewPortNameForTest(const juce::String& name) {
    newNameEditor_.setText(name, juce::dontSendNotification);
}

void MacroPortConfigDialog::setNewPortDirectionForTest(bool isInput) {
    newDirectionBox_.setSelectedId(isInput ? kDirectionInputId : kDirectionOutputId, juce::sendNotification);
}

void MacroPortConfigDialog::setNewPortKindForTest(synth::MacroPortKind kind) {
    newKindBox_.setSelectedId(kind == synth::MacroPortKind::Midi ? kKindMidiId : kKindAudioCVId,
                              juce::sendNotification);
}

void MacroPortConfigDialog::setNewPortShapeForTest(MacroPortShape shape) {
    newShapeBox_.setSelectedId(comboIndexFromShape(shape), juce::sendNotification);
}

void MacroPortConfigDialog::setNewPortVoiceCountForTest(int voices) {
    newVoicesEditor_.setText(juce::String(voices), juce::dontSendNotification);
}

void MacroPortConfigDialog::triggerAddPortForTest() {
    if (!onAddPort)
        return;
    const bool isInput = newDirectionBox_.getSelectedId() == kDirectionInputId;
    const bool isMidi = newKindBox_.getSelectedId() == kKindMidiId;
    const auto kind = isMidi ? synth::MacroPortKind::Midi : synth::MacroPortKind::AudioCV;
    const auto shape = shapeFromComboIndex(newShapeBox_.getSelectedId());
    const int voices = juce::jmax(1, newVoicesEditor_.getText().getIntValue());
    onAddPort(isInput, kind, shape, voices, newNameEditor_.getText());
}

void MacroPortConfigDialog::setRowNameForTest(int row, const juce::String& name) {
    if (row >= 0 && row < (int)rowControls_.size())
        rowControls_[row]->nameEditor.setText(name, juce::dontSendNotification);
}

void MacroPortConfigDialog::commitRowNameForTest(int row) {
    if (row >= 0 && row < (int)rowControls_.size() && onRenamePort)
        onRenamePort(getRowNodeUuidForTest(row), rowControls_[row]->nameEditor.getText());
}

void MacroPortConfigDialog::triggerRowDeleteForTest(int row) {
    // Calls the REAL onClick handler directly rather than juce::Button::triggerClick(), which
    // posts an async command message (Button::handleCommandMessage) - a headless test with no
    // running message loop would never see it fire.
    if (row >= 0 && row < (int)rowControls_.size() && rowControls_[row]->deleteButton.onClick)
        rowControls_[row]->deleteButton.onClick();
}

void MacroPortConfigDialog::triggerRowMoveUpForTest(int row) {
    if (row >= 0 && row < (int)rowControls_.size() && rowControls_[row]->upButton.onClick)
        rowControls_[row]->upButton.onClick();
}

void MacroPortConfigDialog::triggerRowMoveDownForTest(int row) {
    if (row >= 0 && row < (int)rowControls_.size() && rowControls_[row]->downButton.onClick)
        rowControls_[row]->downButton.onClick();
}

void MacroPortConfigDialog::setRowShapeForTest(int row, MacroPortShape shape) {
    if (row >= 0 && row < (int)rowControls_.size())
        rowControls_[row]->shapeBox.setSelectedId(comboIndexFromShape(shape), juce::sendNotification);
}

void MacroPortConfigDialog::setRowVoiceCountForTest(int row, int voices) {
    if (row >= 0 && row < (int)rowControls_.size())
        rowControls_[row]->voicesEditor.setText(juce::String(voices), juce::dontSendNotification);
}

void MacroPortConfigDialog::triggerRowApplyShapeForTest(int row) {
    if (row >= 0 && row < (int)rowControls_.size() && rowControls_[row]->applyShapeButton.onClick)
        rowControls_[row]->applyShapeButton.onClick();
}

void MacroPortConfigDialog::triggerCloseForTest() {
    if (closeButton_.onClick)
        closeButton_.onClick();
}

} // namespace synth::ui
