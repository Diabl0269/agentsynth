#include "MacroPortConfigDialog.h"
#include "Theme/AppLookAndFeel.h"

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

constexpr int kGlyphButtonSize = 20;
constexpr int kGlyphButtonGap = 2;

// Sets a combo box's selection and calls its REAL onChange handler directly, rather than via
// juce::ComboBox's own sendNotification path — that posts through AsyncUpdater, which a headless
// test's message-less run loop never pumps, so the change would silently never fire. Same idiom
// triggerRowDeleteForTest's comment already documents for juce::Button::triggerClick(). Every
// *ForTest seam that flips a combo box goes through this so "drive the real control" also means
// "and see its real, synchronous side effects" regardless of whether a message loop is running.
void setComboSelectionForTest(juce::ComboBox& box, int itemId) {
    box.setSelectedId(itemId, juce::dontSendNotification);
    if (box.onChange)
        box.onChange();
}

// Resolves the live theme's colour tokens, evaluated fresh at every call site (never cached) so a
// theme switch or this dialog being reparented mid-life is never stale — the same reasoning
// PreferencesSettingsTab's popup content and MidiDestinationPicker give for the identical
// dynamic_cast, and why it is done here at PAINT time rather than once at construction (a
// juce::DialogWindow's content component is not guaranteed to already sit under
// synth::theme::AppLookAndFeel the moment its constructor runs).
const synth::theme::Colors& liveThemeColours(const juce::Component& c) {
    static const synth::theme::Colors fallback{};
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&c.getLookAndFeel()))
        return lf->getTheme().colors;
    return fallback;
}

// A compact icon-style affordance replacing the old full-width "Up"/"Down"/"Delete" text buttons
// (founder review item 1) — a small square button drawing one glyph as a filled juce::Path, the
// same "drawn Path, not an SVG asset" idiom MacroCardComponent's own expand chevron already uses,
// so this adds no new themed icon asset for three small per-row affordances.
class GlyphButton : public juce::Button {
public:
    enum class Glyph { Up, Down, Delete };

    explicit GlyphButton(Glyph glyph)
        : juce::Button(juce::String())
        , glyph_(glyph) {}

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto& c = liveThemeColours(*this);
        const bool isDelete = glyph_ == Glyph::Delete;
        const juce::Colour hotColour = isDelete ? c.error : c.accent;
        auto bounds = getLocalBounds().toFloat();

        if (isEnabled() && (highlighted || down)) {
            g.setColour(hotColour.withAlpha(down ? 0.28f : 0.15f));
            g.fillRoundedRectangle(bounds, 4.0f);
        }

        juce::Colour glyphColour = c.textMuted;
        if (!isEnabled())
            glyphColour = c.textDisabled;
        else if (highlighted || down)
            glyphColour = hotColour;
        g.setColour(glyphColour);

        auto inner = bounds.reduced(bounds.getWidth() * 0.3f, bounds.getHeight() * 0.3f);
        switch (glyph_) {
        case Glyph::Up: {
            juce::Path p;
            p.addTriangle(inner.getX(), inner.getBottom(), inner.getRight(), inner.getBottom(), inner.getCentreX(),
                          inner.getY());
            g.fillPath(p);
            break;
        }
        case Glyph::Down: {
            juce::Path p;
            p.addTriangle(inner.getX(), inner.getY(), inner.getRight(), inner.getY(), inner.getCentreX(),
                          inner.getBottom());
            g.fillPath(p);
            break;
        }
        case Glyph::Delete:
            g.drawLine(inner.getX(), inner.getY(), inner.getRight(), inner.getBottom(), 1.6f);
            g.drawLine(inner.getX(), inner.getBottom(), inner.getRight(), inner.getY(), 1.6f);
            break;
        }
    }

private:
    Glyph glyph_;
};
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

// One row's controls, grouped in a single Component so it can paint its own kind-tinted
// background (founder review item 1: "make a MIDI row visually distinct from an audio/CV row") —
// a faint fill plus a coloured left accent bar, using the SAME jack-colour convention
// MacroCardComponent's own port dots already use (MIDI -> audioWire, AudioCV -> accent; see that
// file's paint() comment for why that pairing, counter-intuitive as it reads, is deliberate).
//
// Every callback below reaches straight into `owner`'s std::function members rather than storing
// its own copies — `owner` is the MacroPortConfigDialog that owns this row through
// rowControls_ (an OwnedArray), so it strictly outlives every row and a bound reference is safe,
// exactly like the pre-redesign code capturing `this` (the dialog) in each row's lambdas.
class MacroPortConfigDialog::PortRowComponent : public juce::Component {
public:
    PortRowComponent(MacroPortConfigDialog& owner, const PortRow& row)
        : nodeUuid(row.nodeUuid)
        , isMidi(row.kind == synth::MacroPortKind::Midi)
        , upButton(GlyphButton::Glyph::Up)
        , downButton(GlyphButton::Glyph::Down)
        , deleteButton(GlyphButton::Glyph::Delete)
        , owner_(owner)
        , committedShape_(row.shape)
        , committedVoices_(juce::jmax(1, row.voiceCount)) {
        nameEditor.setText(row.name, juce::dontSendNotification);
        nameEditor.setJustification(juce::Justification::centredLeft);
        nameEditor.setFont(juce::Font(juce::FontOptions(12.5f)));
        nameEditor.onFocusLost = [this] {
            if (owner_.onRenamePort)
                owner_.onRenamePort(nodeUuid, nameEditor.getText());
        };
        nameEditor.onReturnKey = nameEditor.onFocusLost;
        addAndMakeVisible(nameEditor);

        midiTag.setText("MIDI", juce::dontSendNotification);
        midiTag.setJustificationType(juce::Justification::centred);
        midiTag.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        midiTag.setVisible(isMidi);
        addAndMakeVisible(midiTag);

        populateShapeBox(shapeBox);
        shapeBox.setSelectedId(comboIndexFromShape(row.shape), juce::dontSendNotification);
        shapeBox.setVisible(!isMidi);
        // Founder review item 1: the combo box IS the "Apply Shape" gesture now — selecting a new
        // shape commits immediately (still delete+re-add of the node as ONE undo step underneath,
        // per GraphEditor::changeMacroPortShape; only the UI gesture collapsed from two steps to
        // one, per the class comment). maybeCommitShape guards against firing on a no-op (see its
        // own comment) — load-bearing here because GraphEditor::changeMacroPortShape does not
        // early-out on an unchanged (shape, voiceCount) pair itself: it always deletes and
        // re-creates the node, minting a FRESH nodeUuid, in the one subsystem
        // (docs/macros.md §5.2) that is built entirely on uuid identity.
        shapeBox.onChange = [this] {
            updateVoicesVisibility();
            maybeCommitShape();
        };
        addAndMakeVisible(shapeBox);

        voicesLabel.setText("Voices", juce::dontSendNotification);
        voicesLabel.setJustificationType(juce::Justification::centredRight);
        voicesLabel.setFont(juce::Font(juce::FontOptions(9.5f)));
        addAndMakeVisible(voicesLabel);

        voicesEditor.setText(juce::String(committedVoices_), juce::dontSendNotification);
        voicesEditor.setInputRestrictions(2, "0123456789");
        voicesEditor.setJustification(juce::Justification::centred);
        // Unlike a combo box (which only notifies on an actual selection change), onFocusLost/
        // onReturnKey fire on every transit through the field — tabbing past it, or clicking Close
        // right after it, loses focus with nothing typed. maybeCommitShape's guard is what keeps
        // that from re-minting the port's node on a no-op (see its own comment).
        voicesEditor.onFocusLost = [this] { maybeCommitShape(); };
        voicesEditor.onReturnKey = voicesEditor.onFocusLost;
        addAndMakeVisible(voicesEditor);

        upButton.setTooltip("Move up");
        upButton.onClick = [this] {
            if (owner_.onReorderPort)
                owner_.onReorderPort(nodeUuid, /*moveUp=*/true);
        };
        addAndMakeVisible(upButton);

        downButton.setTooltip("Move down");
        downButton.onClick = [this] {
            if (owner_.onReorderPort)
                owner_.onReorderPort(nodeUuid, /*moveUp=*/false);
        };
        addAndMakeVisible(downButton);

        deleteButton.setTooltip("Delete this port");
        deleteButton.onClick = [this] {
            if (owner_.onDeletePort)
                owner_.onDeletePort(nodeUuid);
        };
        addAndMakeVisible(deleteButton);

        updateVoicesVisibility();
    }

    void updateVoicesVisibility() {
        const bool poly = !isMidi && shapeFromComboIndex(shapeBox.getSelectedId()) == MacroPortShape::Poly;
        voicesEditor.setVisible(poly);
        voicesLabel.setVisible(poly);
        resized();
    }

    int currentVoiceCount() const { return juce::jmax(1, voicesEditor.getText().getIntValue()); }

    // Fires onChangePortShape only when the (shape, voiceCount) pair the controls currently read
    // actually differs from what was last committed — a real juce::ComboBox already only notifies
    // on an actual selection change, but the voices TextEditor's onFocusLost/onReturnKey do not
    // have that property (see their call sites' comments), and GraphEditor::changeMacroPortShape
    // itself has no such guard: it unconditionally deletes and re-creates the node. The committed
    // pair is updated HERE, synchronously, rather than only once refreshPorts() rebuilds this row
    // from the graph — GraphEditor wraps onChangePortShape in MessageManager::callAsync, so a
    // Return keypress immediately followed by a focus-lost (pressing Enter, then clicking Close)
    // would otherwise queue a second commit against the same stale baseline before the first one's
    // async round-trip has rebuilt anything.
    void maybeCommitShape() {
        const auto newShape = shapeFromComboIndex(shapeBox.getSelectedId());
        const int newVoices = currentVoiceCount();
        if (newShape == committedShape_ && newVoices == committedVoices_)
            return;
        committedShape_ = newShape;
        committedVoices_ = newVoices;
        if (owner_.onChangePortShape)
            owner_.onChangePortShape(nodeUuid, newShape, newVoices);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(6, 3);

        auto placeGlyph = [&](GlyphButton& btn) {
            btn.setBounds(
                area.removeFromRight(kGlyphButtonSize).withSizeKeepingCentre(kGlyphButtonSize, kGlyphButtonSize));
            area.removeFromRight(kGlyphButtonGap);
        };
        placeGlyph(deleteButton);
        placeGlyph(downButton);
        placeGlyph(upButton);
        area.removeFromRight(8);

        if (isMidi) {
            midiTag.setBounds(area.removeFromRight(56));
        } else {
            if (voicesEditor.isVisible()) {
                voicesEditor.setBounds(area.removeFromRight(38));
                area.removeFromRight(4);
                voicesLabel.setBounds(area.removeFromRight(42));
                area.removeFromRight(6);
            }
            shapeBox.setBounds(area.removeFromRight(90));
        }
        area.removeFromRight(8);
        nameEditor.setBounds(area);
    }

    void paint(juce::Graphics& g) override {
        const auto& c = liveThemeColours(*this);
        const juce::Colour kindColour = isMidi ? c.audioWire : c.accent;
        auto bounds = getLocalBounds().toFloat();

        if (isMidi) {
            g.setColour(c.midiWire.withAlpha(0.08f));
            g.fillRoundedRectangle(bounds, 5.0f);
        }

        g.setColour(kindColour.withAlpha(0.6f));
        g.fillRoundedRectangle(bounds.withWidth(3.0f).reduced(0.0f, 3.0f), 1.5f);
    }

    juce::String nodeUuid;
    bool isMidi = false;

    juce::TextEditor nameEditor;
    juce::Label midiTag{"midiTag", juce::String()};
    juce::ComboBox shapeBox; // AudioCV rows only; hidden entirely for a MIDI row
    juce::Label voicesLabel{"voicesLabel", juce::String()};
    juce::TextEditor voicesEditor; // shown only while shapeBox reads Poly-N
    GlyphButton upButton;
    GlyphButton downButton;
    GlyphButton deleteButton;

private:
    MacroPortConfigDialog& owner_; // outlives this row: owned by owner_.rowControls_
    MacroPortShape committedShape_;
    int committedVoices_;
};

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
    addAndMakeVisible(newVoicesEditor_);
    updateNewPortVoicesVisibility(); // Mono is the default shape: starts hidden

    newNameEditor_.setTextToShowWhenEmpty("Port name", juce::Colours::grey);
    addAndMakeVisible(newNameEditor_);

    addButton_.onClick = [this] { triggerAddPortForTest(); };
    addAndMakeVisible(addButton_);

    closeButton_.onClick = [this] {
        if (onRequestClose)
            onRequestClose();
    };
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

    auto addBlockArea = area.removeFromTop(8 + kAddRowHeight + 6 + kAddRowHeight + 8);
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
    const int chromeHeight = kMargin * 2                                   // outer margins
                             + 24 + 10                                     // title + gap
                             + (8 + kAddRowHeight + 6 + kAddRowHeight + 8) // "Add a port" block
                             + 10                                          // gap before the row list
                             + 6 + kAddRowHeight + 6;                      // gap + Close row + gap
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
    setComboSelectionForTest(newDirectionBox_, isInput ? kDirectionInputId : kDirectionOutputId);
}

void MacroPortConfigDialog::setNewPortKindForTest(synth::MacroPortKind kind) {
    setComboSelectionForTest(newKindBox_, kind == synth::MacroPortKind::Midi ? kKindMidiId : kKindAudioCVId);
}

void MacroPortConfigDialog::setNewPortShapeForTest(MacroPortShape shape) {
    setComboSelectionForTest(newShapeBox_, comboIndexFromShape(shape));
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
    if (row >= 0 && row < (int)rowControls_.size() && rowControls_[row]->nameEditor.onFocusLost)
        rowControls_[row]->nameEditor.onFocusLost();
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
    // setComboSelectionForTest calls shapeBox.onChange directly, which now IS the commit gesture
    // (see the class comment), so this alone reproduces the real "pick a new shape" click.
    if (row >= 0 && row < (int)rowControls_.size())
        setComboSelectionForTest(rowControls_[row]->shapeBox, comboIndexFromShape(shape));
}

void MacroPortConfigDialog::setRowVoiceCountForTest(int row, int voices) {
    if (row >= 0 && row < (int)rowControls_.size())
        rowControls_[row]->voicesEditor.setText(juce::String(voices), juce::dontSendNotification);
}

void MacroPortConfigDialog::commitRowVoiceCountForTest(int row) {
    if (row >= 0 && row < (int)rowControls_.size() && rowControls_[row]->voicesEditor.onFocusLost)
        rowControls_[row]->voicesEditor.onFocusLost();
}

void MacroPortConfigDialog::triggerCloseForTest() {
    if (closeButton_.onClick)
        closeButton_.onClick();
}

// ---- MacroAutoPortPromptDialog (founder-review fix F5, docs/macros.md §7 item 6.2) -------------

MacroAutoPortPromptDialog::MacroAutoPortPromptDialog(int crossingPortCount) {
    titleLabel_.setText("Macro has boundary cables", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel_);

    const juce::String plural = crossingPortCount == 1 ? juce::String() : juce::String("s");
    messageLabel_.setText("This macro will need " + juce::String(crossingPortCount) + " port" + plural +
                              " for the cables crossing its boundary. Create the port" + plural +
                              ", or leave the cables exactly as they are?",
                          juce::dontSendNotification);
    messageLabel_.setFont(juce::Font(juce::FontOptions(13.0f)));
    messageLabel_.setJustificationType(juce::Justification::topLeft);
    messageLabel_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(messageLabel_);

    // Opt-out, not opt-in: most users making this choice want it applied from now on, and "always
    // ask" stays one click away in Preferences for anyone who wants to reconsider every time.
    rememberToggle_.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(rememberToggle_);

    createPortsButton_.onClick = [this] { triggerCreatePortsForTest(); };
    addAndMakeVisible(createPortsButton_);

    leaveAsIsButton_.onClick = [this] { triggerLeaveCablesAsIsForTest(); };
    addAndMakeVisible(leaveAsIsButton_);

    setSize(kWidth, 190);
}

MacroAutoPortPromptDialog::~MacroAutoPortPromptDialog() = default;

void MacroAutoPortPromptDialog::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void MacroAutoPortPromptDialog::resized() {
    auto area = getLocalBounds().reduced(kMargin);
    titleLabel_.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    messageLabel_.setBounds(area.removeFromTop(64));
    area.removeFromTop(10);
    rememberToggle_.setBounds(area.removeFromTop(22));
    area.removeFromTop(14);

    auto buttonRow = area.removeFromTop(30);
    leaveAsIsButton_.setBounds(buttonRow.removeFromRight(150));
    buttonRow.removeFromRight(8);
    createPortsButton_.setBounds(buttonRow.removeFromRight(130));
}

void MacroAutoPortPromptDialog::setRememberChoiceForTest(bool remember) {
    rememberToggle_.setToggleState(remember, juce::dontSendNotification);
}

bool MacroAutoPortPromptDialog::getRememberChoiceForTest() const { return rememberToggle_.getToggleState(); }

void MacroAutoPortPromptDialog::triggerCreatePortsForTest() {
    if (onChoice)
        onChoice(true, rememberToggle_.getToggleState());
}

void MacroAutoPortPromptDialog::triggerLeaveCablesAsIsForTest() {
    if (onChoice)
        onChoice(false, rememberToggle_.getToggleState());
}

} // namespace synth::ui
