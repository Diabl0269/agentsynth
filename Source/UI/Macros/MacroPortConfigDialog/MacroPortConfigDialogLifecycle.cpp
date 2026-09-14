#include "MacroPortConfigDialog.h"
#include "MacroPortConfigDialogInternal.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

// Concern: construction, layout, paint, and the shape combo-box helpers.

namespace {
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
    // StereoCollapsed is auto-derived-only (MacroPortShape.h) and never a choice this combo box
    // offers (populateShapeBox has no entry for it), but an EXISTING auto-created port can still
    // show up in this dialog, and it genuinely IS a stereo pair — display it as "Stereo" rather
    // than falling through to the Mono default below. Because shapeFromComboIndex() can never
    // produce StereoCollapsed, any real interaction with this row (even re-picking "Stereo") is a
    // deliberate, explicit shape choice and correctly converts it to the two-jack Stereo shape —
    // that is the intended behaviour, not a display bug.
    case MacroPortShape::StereoCollapsed:
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
    // Founder review item 1: the window's own native title bar already reads "Configure I/O", so
    // the in-dialog title no longer repeats it — just the macro's name, which the chrome cannot
    // show.
    titleLabel_.setText(macroName_.isNotEmpty() ? macroName_ : "Macro", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel_);

    newPortSectionLabel_.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    addAndMakeVisible(newPortSectionLabel_);

    newDirectionBox_.addItem("Input", kDirectionInputId);
    newDirectionBox_.addItem("Output", kDirectionOutputId);
    newDirectionBox_.setSelectedId(kDirectionInputId, juce::dontSendNotification);
    addAndMakeVisible(newDirectionBox_);

    newKindBox_.addItem("Audio / CV", kKindAudioCVId);
    newKindBox_.addItem("MIDI", kKindMidiId);
    newKindBox_.setSelectedId(kKindAudioCVId, juce::dontSendNotification);
    newKindBox_.onChange = [this] {
        newShapeBox_.setVisible(newKindBox_.getSelectedId() != kKindMidiId);
        updateNewPortVoicesVisibility();
    };
    addAndMakeVisible(newKindBox_);

    populateShapeBox(newShapeBox_);
    newShapeBox_.setSelectedId(kShapeMonoId, juce::dontSendNotification);
    newShapeBox_.onChange = [this] { updateNewPortVoicesVisibility(); };
    addAndMakeVisible(newShapeBox_);

    newVoicesLabel_.setJustificationType(juce::Justification::centredRight);
    newVoicesLabel_.setFont(juce::Font(juce::FontOptions(9.5f)));
    addAndMakeVisible(newVoicesLabel_);

    newVoicesEditor_.setText("4", juce::dontSendNotification);
    newVoicesEditor_.setInputRestrictions(2, "0123456789");
    newVoicesEditor_.setJustification(juce::Justification::centred);
    newVoicesEditor_.onReturnKey = [this] { triggerAddPortForTest(); }; // T153, same as newNameEditor_
    newVoicesEditor_.onEscapeKey = [this] { requestClose(); };
    addAndMakeVisible(newVoicesEditor_);
    updateNewPortVoicesVisibility(); // Mono is the default shape: starts hidden

    newNameEditor_.setTextToShowWhenEmpty("Port name", juce::Colours::grey);
    // T153: Return commits the in-progress "Add a port" field the same way it already does for a
    // row's rename/voices fields — pressing Return here is the keyboard equivalent of clicking Add.
    newNameEditor_.onReturnKey = [this] { triggerAddPortForTest(); };
    newNameEditor_.onEscapeKey = [this] { // same "Escape closes the whole modal" decision as elsewhere
        requestClose();
    };
    addAndMakeVisible(newNameEditor_);

    addButton_.onClick = [this] { triggerAddPortForTest(); };
    addAndMakeVisible(addButton_);

    closeButton_.onClick = [this] { requestClose(); };
    addAndMakeVisible(closeButton_);

    addAndMakeVisible(rowsViewport_);
    rowsViewport_.setViewedComponent(&rowsContent_, false);
    rowsViewport_.setScrollBarsShown(true, false);

    inputsHeader_.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    outputsHeader_.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    rowsContent_.addAndMakeVisible(inputsHeader_);
    rowsContent_.addAndMakeVisible(outputsHeader_);

    inputsEmptyHint_.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::italic)));
    outputsEmptyHint_.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::italic)));
    rowsContent_.addAndMakeVisible(inputsEmptyHint_);
    rowsContent_.addAndMakeVisible(outputsEmptyHint_);

    rebuildRowComponents();
    setSize(kDialogWidth, idealDialogHeight());
    resized();
}

MacroPortConfigDialog::~MacroPortConfigDialog() = default;

// T153: the bubble-up path — reached whenever the currently-focused control does not itself
// consume the key (a ComboBox or a GlyphButton/PortColourSwatch with no unhandled arrow, or
// nothing focused at all). A juce::TextEditor consumes Escape/Return itself before either ever
// gets here (TextEditor::keyPressed returns true for both), which is why every TextEditor above
// ALSO gets its own onEscapeKey wired directly rather than relying on this alone.
bool MacroPortConfigDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        requestClose();
        return true;
    }
    return false;
}

// T150: force any in-flight row-editor edit (rename, voice count) to commit before the dialog
// closes. TextEditor::focusLost() posts an async command message rather than calling
// onFocusLost synchronously (juce_TextEditor.cpp) — Close's own mouseDown already grabbed
// keyboard focus away from whichever row editor had it (Component::internalMouseDown always
// does this), so that editor's onFocusLost is already QUEUED but has not run yet by the time
// onRequestClose would fire. Racing onRequestClose (which tears the dialog down) against that
// queued async commit is exactly the founder-reported bug: the rename either never lands or
// lands late, after the dialog already looks closed. Calling each row's own commit method
// directly and synchronously here — the same idiom every *ForTest commit seam in this file
// already uses — sidesteps the race entirely. maybeCommitName()/maybeCommitVoicesOnClose() are
// both no-ops when nothing actually changed (or nothing is eligible to have changed), so it is
// safe to call this unconditionally for every row regardless of which one (if any) currently has
// focus. Deliberately NOT maybeCommitShape() directly — see maybeCommitVoicesOnClose()'s own
// comment for why re-deriving "the current shape" from the combo at close time is unsafe for a
// StereoCollapsed row.
//
// Safe against rowControls_ being torn down mid-loop only because every real onRenamePort/
// onChangePortShape (GraphEditor::promptConfigureMacroIO) defers its refreshPorts()/
// rebuildRowComponents() through MessageManager::callAsync rather than calling it synchronously
// from inside the callback — an invariant that file's own wiring comment states explicitly. A
// callback that broke that invariant (e.g. a test firing refreshPorts() synchronously) would
// leave `rc` dangling for the rest of this loop.
void MacroPortConfigDialog::requestClose() {
    for (auto* rc : rowControls_) {
        rc->maybeCommitName();
        rc->maybeCommitVoicesOnClose();
    }
    if (onRequestClose)
        onRequestClose();
}

void MacroPortConfigDialog::updateNewPortVoicesVisibility() {
    const bool isMidi = newKindBox_.getSelectedId() == kKindMidiId;
    const bool poly = !isMidi && newShapeBox_.getSelectedId() == kShapePolyId;
    newVoicesEditor_.setVisible(poly);
    newVoicesLabel_.setVisible(poly);
}

void MacroPortConfigDialog::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));

    const auto& c = liveThemeColours(*this);

    // "Add a port" panel — a faintly bordered, rounded group so the row of controls above the Add
    // button reads as one tied-together block (founder review item 1) rather than floating loose
    // above an unrelated Add button.
    if (!addBlockBounds_.isEmpty()) {
        g.setColour(c.surface.withAlpha(0.5f));
        g.fillRoundedRectangle(addBlockBounds_.toFloat(), 8.0f);
        g.setColour(c.border);
        g.drawRoundedRectangle(addBlockBounds_.toFloat().reduced(0.5f), 8.0f, 1.0f);
    }
}

void MacroPortConfigDialog::resized() {
    auto area = getLocalBounds().reduced(kMargin);

    titleLabel_.setBounds(area.removeFromTop(24));
    area.removeFromTop(10);

    auto addBlockArea = area.removeFromTop(kAddBlockHeight);
    addBlockBounds_ = addBlockArea;
    auto addBlock = addBlockArea.reduced(8, 8);

    newPortSectionLabel_.setBounds(addBlock.removeFromTop(kAddRowHeight));

    auto newRow1 = addBlock.removeFromTop(kAddRowHeight);
    newDirectionBox_.setBounds(newRow1.removeFromLeft(96));
    newRow1.removeFromLeft(6);
    newKindBox_.setBounds(newRow1.removeFromLeft(100));
    newRow1.removeFromLeft(6);
    if (newShapeBox_.isVisible()) {
        newShapeBox_.setBounds(newRow1.removeFromLeft(84));
        newRow1.removeFromLeft(6);
    }
    if (newVoicesEditor_.isVisible()) {
        newVoicesLabel_.setBounds(newRow1.removeFromLeft(40));
        newRow1.removeFromLeft(4);
        newVoicesEditor_.setBounds(newRow1.removeFromLeft(38));
    }

    addBlock.removeFromTop(6);
    auto newRow2 = addBlock.removeFromTop(kAddRowHeight);
    addButton_.setBounds(newRow2.removeFromRight(72));
    newRow2.removeFromRight(6);
    newNameEditor_.setBounds(newRow2);

    area.removeFromTop(10);

    auto closeRow = area.removeFromBottom(kAddRowHeight + 6);
    closeRow.removeFromTop(6);
    closeButton_.setBounds(closeRow.removeFromRight(84));

    area.removeFromBottom(6);
    rowsViewport_.setBounds(area);
    layOutOrMeasureRows(/*apply=*/true, area.getWidth() - 2);
}

int MacroPortConfigDialog::layOutOrMeasureRows(bool apply, int width) {
    width = juce::jmax(160, width);
    int y = 0;

    if (apply)
        inputsHeader_.setBounds(0, y, width, kSectionHeaderHeight);
    y += kSectionHeaderHeight;

    bool anyInput = false;
    for (int i = 0; i < (int)rows_.size(); ++i) {
        if (!rows_[(size_t)i].isInput)
            continue;
        anyInput = true;
        if (apply)
            rowControls_[i]->setBounds(0, y, width, kRowHeight);
        y += kRowHeight + kRowGap;
    }
    if (apply)
        inputsEmptyHint_.setVisible(!anyInput);
    if (!anyInput) {
        if (apply)
            inputsEmptyHint_.setBounds(0, y, width, kEmptyHintHeight);
        y += kEmptyHintHeight;
    }

    y += kSectionGap;
    if (apply)
        outputsHeader_.setBounds(0, y, width, kSectionHeaderHeight);
    y += kSectionHeaderHeight;

    bool anyOutput = false;
    for (int i = 0; i < (int)rows_.size(); ++i) {
        if (rows_[(size_t)i].isInput)
            continue;
        anyOutput = true;
        if (apply)
            rowControls_[i]->setBounds(0, y, width, kRowHeight);
        y += kRowHeight + kRowGap;
    }
    if (apply)
        outputsEmptyHint_.setVisible(!anyOutput);
    if (!anyOutput) {
        if (apply)
            outputsEmptyHint_.setBounds(0, y, width, kEmptyHintHeight);
        y += kEmptyHintHeight;
    }

    if (apply)
        rowsContent_.setSize(width, y);
    return y;
}

int MacroPortConfigDialog::idealDialogHeight() {
    const int rowsHeight = layOutOrMeasureRows(/*apply=*/false, kDialogWidth - kMargin * 2 - 2);
    const int chromeHeight = kMargin * 2              // outer margins
                             + 24 + 10                // title + gap
                             + kAddBlockHeight        // "Add a port" block
                             + 10                     // gap before the row list
                             + 6 + kAddRowHeight + 6; // gap + Close row + gap
    return juce::jlimit(kMinDialogHeight, kMaxDialogHeight, chromeHeight + rowsHeight);
}

void MacroPortConfigDialog::rebuildRowComponents() {
    rowControls_.clear();

    for (const auto& row : rows_) {
        auto* rc = new PortRowComponent(*this, row);
        rowControls_.add(rc);
        rowsContent_.addAndMakeVisible(rc);
    }
}

void MacroPortConfigDialog::refreshPorts(std::vector<PortRow> ports) {
    rows_ = std::move(ports);
    rebuildRowComponents();
    setSize(kDialogWidth, idealDialogHeight());
    resized();
    repaint();
}

} // namespace synth::ui
