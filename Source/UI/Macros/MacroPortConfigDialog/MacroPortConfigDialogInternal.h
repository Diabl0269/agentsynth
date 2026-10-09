#pragma once

// Shared between the split MacroPortConfigDialog*.cpp units: the "Add a port" combo-box item ids
// (constructed in Lifecycle.cpp, read back in TestSeams.cpp), and — because it is a
// private nested class of MacroPortConfigDialog whose members Lifecycle.cpp (rebuildRowComponents),
// RowOrdering.cpp (drag/keyboard nav), and TestSeams.cpp all need the complete type for — the row
// widgets (GlyphButton, PortColourSwatch, DragHandle) and PortRowComponent itself.

#include "MacroPortConfigDialog.h"
#include "UI/Layout/ColourSwatchButton.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/IconButton.h"
#include "UI/Layout/ReorderDrag/ReorderLiftLook.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
// ComboBox item ids are 1-based in JUCE.
constexpr int kDirectionInputId = 1;
constexpr int kDirectionOutputId = 2;

constexpr int kKindAudioCVId = 1;
constexpr int kKindMidiId = 2;
} // namespace

namespace {
// Up/Down on a control that opts in (the row's colour swatch and Delete glyph button — see
// MacroPortConfigDialog::moveRowFocus's comment for why arrow navigation is scoped to only those)
// reports itself as an arrow key rather than the caller re-deriving KeyPress comparisons at every
// call site. GlyphButton/PortColourSwatch::keyPressed also check the command modifier themselves
// (the Cmd+Up/Cmd+Down reorder chord) before falling through to this.
inline bool isVerticalArrowKey(const juce::KeyPress& key, bool& outMoveDown) {
    if (key.isKeyCode(juce::KeyPress::downKey)) {
        outMoveDown = true;
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::upKey)) {
        outMoveDown = false;
        return true;
    }
    return false;
}

constexpr int kGlyphButtonSize = 20;
constexpr int kGlyphButtonGap = 2;
constexpr int kColourSwatchSize = 16;
constexpr int kDragHandleWidth = 14;

// The row's Delete button: the shared IconButton (Danger style, the drawn X glyph). Cmd+Up/Cmd+Down
// on this button is the keyboard-accessible reorder (see PortRowComponent's onReorderChord wiring
// below).
class GlyphButton : public IconButton {
public:
    enum class Glyph { Delete };

    explicit GlyphButton(Glyph)
        : IconButton(juce::String(), synth::theme::Glyph::Delete, Style::Danger) {}

    // The Delete button is the keyboard-accessible delete fallback (it gets Tab/Return/Space for
    // free from juce::Button) — this adds Up/Down-arrow FOCUS navigation between rows on top, wired
    // by PortRowComponent to MacroPortConfigDialog::moveRowFocus. Cmd+Up/Cmd+Down is checked FIRST
    // and fires onReorderChord instead — a bare arrow only moves focus. Returning false when
    // nothing is wired (or the key isn't an arrow) falls through to Button::keyPressed so
    // Return/Space keep triggering the click.
    bool keyPressed(const juce::KeyPress& key) override {
        bool moveDown = false;
        if (isVerticalArrowKey(key, moveDown)) {
            if (key.getModifiers().isCommandDown()) {
                if (onReorderChord) {
                    onReorderChord(moveDown);
                    return true;
                }
            } else if (onVerticalArrow) {
                onVerticalArrow(moveDown);
                return true;
            }
        }
        return IconButton::keyPressed(key);
    }

    std::function<void(bool moveDown)> onVerticalArrow;
    std::function<void(bool moveDown)> onReorderChord; // Cmd+Up/Cmd+Down
};

// The row's kind-tinted left-edge swatch — left-click opens a synth::ui::ColourPickerPopup (the
// same favourites-shelf picker TimelineTrackHeaderComponent's track colour swatch and
// AppearanceSettingsTab's note swatches already use), right-click resets to the kind-tint default. The shared
// ColourSwatchButton paints and handles the right-click; this adds the row's arrow-key navigation.
// `colour` is what actually PAINTS (custom colour, or the kind tint fallback); PortRowComponent is
// the one that decides which of those it currently is.
class PortColourSwatch : public ColourSwatchButton {
public:
    PortColourSwatch()
        : ColourSwatchButton("portColourSwatch") {}

    // Cmd+Up/Cmd+Down is checked first and fires onReorderChord (keyboard-accessible reordering); a
    // bare arrow only moves focus via onVerticalArrow — see GlyphButton's identical override for
    // the full reasoning.
    bool keyPressed(const juce::KeyPress& key) override {
        bool moveDown = false;
        if (isVerticalArrowKey(key, moveDown)) {
            if (key.getModifiers().isCommandDown()) {
                if (onReorderChord) {
                    onReorderChord(moveDown);
                    return true;
                }
            } else if (onVerticalArrow) {
                onVerticalArrow(moveDown);
                return true;
            }
        }
        return ColourSwatchButton::keyPressed(key);
    }

    std::function<void(bool moveDown)> onVerticalArrow;
    std::function<void(bool moveDown)> onReorderChord; // Cmd+Up/Cmd+Down
};

// The drag-to-reorder handle — a small grip icon to the left of the name editor. Deliberately a
// plain juce::Component, not a juce::Button: dragging is mouse-only by design (Cmd+Up/Cmd+Down on
// the colour swatch or Delete button is the keyboard-accessible fallback, so this handle never
// needs to be a tab stop), and a plain Component sidesteps Button's own click-vs-drag heuristics
// entirely rather than fighting them.
class DragHandle
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    DragHandle() { setMouseCursor(dragGrabCursor()); } // the grab hand on hover and while dragging

    void paint(juce::Graphics& g) override {
        const auto& c = synth::theme::themeOf(*this).colors;
        const juce::Colour grip = dragging_ ? c.accent : (isMouseOver() ? c.textPrimary.withAlpha(0.85f) : c.textMuted);
        g.setColour(grip);
        auto bounds = getLocalBounds().toFloat();
        const float w = bounds.getWidth() * 0.7f;
        const float x = bounds.getCentreX() - w * 0.5f;
        for (int i = 0; i < 3; ++i) {
            const float y = bounds.getY() + bounds.getHeight() * (0.26f + 0.24f * (float)i);
            g.drawLine(x, y, x + w, y, 1.5f);
        }
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    // The events go up whole: the owner converts each one into its own list coordinates, because the
    // handle moves with its row while the row is dragged.
    void mouseDown(const juce::MouseEvent& e) override {
        dragging_ = true;
        repaint();
        if (onDragStart)
            onDragStart(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (onDragMove)
            onDragMove(e);
    }

    void mouseUp(const juce::MouseEvent&) override {
        dragging_ = false;
        repaint();
        if (onDragEnd)
            onDragEnd();
    }

    std::function<void(const juce::MouseEvent&)> onDragStart;
    std::function<void(const juce::MouseEvent&)> onDragMove;
    std::function<void()> onDragEnd;

private:
    bool dragging_ = false;
};
} // namespace

// One row's controls, grouped in a single Component so it can paint its own kind-tinted background:
// a MIDI row gets a faint midiWire fill. The row's kind tint (kindTintColour(), which the colour
// swatch falls back to) uses the SAME jack-colour convention MacroCardComponent's own port dots
// already use (MIDI -> audioWire, AudioCV -> accent; see that file's paint() comment for why that
// pairing, counter-intuitive as it reads, is deliberate).
//
// Every callback below reaches straight into `owner`'s std::function members rather than storing
// its own copies — `owner` is the MacroPortConfigDialog that owns this row through
// rowControls_ (an OwnedArray), so it strictly outlives every row and a bound reference is safe,
// the same as capturing `this` (the dialog) in each row's lambdas.
class MacroPortConfigDialog::PortRowComponent : public juce::Component {
public:
    PortRowComponent(MacroPortConfigDialog& owner, const PortRow& row)
        : nodeUuid(row.nodeUuid)
        , isMidi(row.kind == synth::MacroPortKind::Midi)
        , deleteButton(GlyphButton::Glyph::Delete)
        , owner_(owner)
        , committedShape_(row.shape)
        , committedVoices_(juce::jmax(1, row.voiceCount))
        , committedName_(row.name) {
        nameEditor.setText(row.name, juce::dontSendNotification);
        nameEditor.setJustification(juce::Justification::centredLeft);
        nameEditor.setFont(juce::Font(juce::FontOptions(12.5f)));
        nameEditor.onFocusLost = [this] { maybeCommitName(); };
        nameEditor.onReturnKey = nameEditor.onFocusLost;
        // Escape closes the WHOLE modal (docs/macros/configure-io.md#keyboard-handling decision on this — see the
        // class comment) rather than just reverting this field's edit, matching Close's own behaviour exactly (a
        // focus-loss side effect during teardown commits whatever text is here, the same as clicking Close already does
        // — Escape does not discard anything Close wouldn't). Wired here (rather than relying on the bubble
        // MacroPortConfigDialog::keyPressed catches) because juce::TextEditor consumes Escape itself before it ever
        // bubbles.
        nameEditor.onEscapeKey = [this] { owner_.escapePressed(); };
        nameEditor.setTitle("Port name");
        nameEditor.setTooltip("Rename this port. Press Return to apply.");
        removeHiddenTabStops(nameEditor);
        addAndMakeVisible(nameEditor);

        midiTag.setText("MIDI", juce::dontSendNotification);
        midiTag.setJustificationType(juce::Justification::centred);
        midiTag.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        midiTag.setVisible(isMidi);
        addAndMakeVisible(midiTag);

        populateShapeBox(shapeBox);
        shapeBox.setSelectedId(comboIndexFromShape(row.shape), juce::dontSendNotification);
        shapeBox.setTitle("Port shape");
        shapeBox.setTooltip("Mono, stereo or poly. Changing it replaces the port.");
        // The combo box IS the shape-commit gesture — selecting a new shape commits immediately
        // (still delete+re-add of the node as ONE undo step underneath, per
        // GraphEditor::changeMacroPortShape). maybeCommitShape guards against firing on a no-op
        // (see its own comment) — load-bearing here because GraphEditor::changeMacroPortShape does
        // not early-out on an unchanged (shape, voiceCount) pair itself: it always deletes and
        // re-creates the node, minting a FRESH nodeUuid, in the one subsystem
        // (docs/macros/ports.md#port-set-and-ordering) that is built entirely on uuid identity.
        shapeBox.onChange = [this] {
            updateVoicesVisibility();
            maybeCommitShape();
        };
        addAndMakeVisible(shapeBox);
        shapeBox.setVisible(!isMidi); // a MIDI row has no shape: hidden, so Tab skips it too

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
        voicesEditor.onEscapeKey = [this] { // same reasoning as nameEditor's onEscapeKey above
            owner_.escapePressed();
        };
        voicesEditor.setTitle("Voice count");
        voicesEditor.setTooltip("Number of voices of this poly port. Press Return to apply.");
        removeHiddenTabStops(voicesEditor);
        addAndMakeVisible(voicesEditor);

        // The per-port colour swatch — left-click opens a ColourPickerPopup, right-click
        // resets to the kind-tint default. Starts from whatever the row was constructed with
        // (nullopt for every uncoloured port), never anything the dialog invents.
        customColour = row.colour;
        colourSwatch.colour = customColour.value_or(kindTintColour());
        colourSwatch.setTitle("Port colour");
        colourSwatch.setTooltip("Port colour (right-click to reset)");
        colourSwatch.onClick = [this] { showColourPicker(); };
        colourSwatch.onRightClick = [this] { commitColour(std::nullopt); };
        colourSwatch.onVerticalArrow = [this](bool moveDown) {
            owner_.moveRowFocus(*this, MacroPortConfigDialog::RowControl::Colour, moveDown);
        };
        // Cmd+Up/Cmd+Down on the colour swatch reorders this port (keyboard-accessible reordering).
        colourSwatch.onReorderChord = [this](bool moveDown) {
            if (owner_.onReorderPort)
                owner_.onReorderPort(nodeUuid, /*moveUp=*/!moveDown);
        };
        addAndMakeVisible(colourSwatch);

        // The drag-to-reorder handle. Cmd+Up/Cmd+Down on the colour swatch or Delete button below
        // is the keyboard route — this is an ADDITIONAL, mouse-only gesture, never a replacement.
        dragHandle.setTooltip("Drag to reorder (or Cmd+Up/Cmd+Down on a focused control)");
        dragHandle.onDragStart = [this](const juce::MouseEvent& e) { owner_.beginRowDrag(*this, e); };
        dragHandle.onDragMove = [this](const juce::MouseEvent& e) { owner_.updateRowDrag(e); };
        dragHandle.onDragEnd = [this] { owner_.endRowDrag(*this); };
        addAndMakeVisible(dragHandle);

        deleteButton.setTitle("Delete port");
        deleteButton.setDescription(row.name);
        deleteButton.setTooltip("Delete this port (Cmd+Up/Cmd+Down to reorder)");
        deleteButton.onClick = [this] {
            if (owner_.onDeletePort)
                owner_.onDeletePort(nodeUuid);
        };
        deleteButton.onVerticalArrow = [this](bool moveDown) {
            owner_.moveRowFocus(*this, MacroPortConfigDialog::RowControl::Delete, moveDown);
        };
        // Cmd+Up/Cmd+Down on the delete button reorders this port (keyboard-accessible reordering).
        deleteButton.onReorderChord = [this](bool moveDown) {
            if (owner_.onReorderPort)
                owner_.onReorderPort(nodeUuid, /*moveUp=*/!moveDown);
        };
        addAndMakeVisible(deleteButton);

        // The controls are centred in the row at different heights, which would sort them by pixel
        // row rather than left to right: Tab visits them in the order they read.
        colourSwatch.setExplicitFocusOrder(1);
        nameEditor.setExplicitFocusOrder(2);
        shapeBox.setExplicitFocusOrder(3);
        voicesEditor.setExplicitFocusOrder(4);
        deleteButton.setExplicitFocusOrder(5);

        voicesFade_.snapTo(isPolyShown()); // what a row starts with does not fade
        voicesFade_.onFrame = [this] { resized(); };
        resized();
    }

    juce::Colour kindTintColour() const {
        const auto& c = synth::theme::themeOf(*this).colors;
        return isMidi ? c.audioWire : c.accent;
    }

    // The ONE commit path for a colour change — the picker's onCommit, the swatch's right-click
    // reset, and setRowColourForTest/resetRowColourForTest (via the dialog) all land here, so they
    // can never disagree about what "committing a colour" actually updates.
    void commitColour(std::optional<juce::Colour> newColour) {
        customColour = newColour;
        colourSwatch.colour = customColour.value_or(kindTintColour());
        colourSwatch.repaint();
        if (owner_.onChangePortColour)
            owner_.onChangePortColour(nodeUuid, newColour);
    }

    // Shared by the real swatch click and createRowColourPickerForTest — mirrors
    // TimelineTrackHeaderComponent::buildColourPicker's own split. onPreview is LOCAL-only (just
    // repaints the swatch) — committing on every drag tick would push a recordGraphAndMacroChange
    // undo entry per pixel of slider movement; the real commit fires once, when the popup closes,
    // exactly like the shape combo's own "commits immediately [on a real, discrete choice]" rule,
    // not on every intermediate value.
    //
    // Both callbacks capture SafePointers, NEVER a raw `this` — the popup they're attached to
    // outlives a single click dispatch (it lives inside a juce::CallOutBox until the user closes
    // it), and this row can be destroyed underneath it at any point in between: any OTHER
    // GraphEditor callback wired on this same dialog (onRenamePort, onChangePortShape, ...) runs
    // via MessageManager::callAsync and unconditionally calls refreshPorts() -> rebuildRowComponents()
    // -> rowControls_.clear(), so simply focusing a different field and then opening this row's
    // picker is enough to have that async rebuild land while the CallOutBox is still open. The two
    // callbacks deliberately use DIFFERENT SafePointer targets: onPreview is purely cosmetic (this
    // row's own swatch), so it no-ops once the row is gone — nothing left to preview. onCommit
    // must still land the user's actual pick even if THIS row died in the meantime (their edit
    // should not be silently discarded just because a rebuild raced the callout), so it goes
    // through the DIALOG (which outlives any one row) plus the port's uuid captured by value,
    // straight to onChangePortColour — never through commitColour(), which needs a live `this`.
    std::unique_ptr<synth::ui::ColourPickerPopup> buildColourPicker() {
        juce::Component::SafePointer<PortRowComponent> safeRow(this);
        juce::Component::SafePointer<MacroPortConfigDialog> safeDialog(&owner_);
        const juce::String uuid = nodeUuid;
        return std::make_unique<synth::ui::ColourPickerPopup>(
            colourSwatch.colour, owner_.colourPickerProps_,
            [safeRow, safeDialog, uuid](juce::Colour c) {
                if (auto* row = safeRow.getComponent()) {
                    row->colourSwatch.colour = c;
                    row->colourSwatch.repaint();
                    // Also preview this port's jack on BOTH surfaces (the docked widget and the collapsed
                    // card) live, so the jack tracks the pick in real time. View-layer only (a preview, never a
                    // MacroPort::colour write -- onCommit below owns the commit), so a drag pushes no undo step.
                    // Captured via safeDialog: the popup outlives one click dispatch just like onCommit; the optional
                    // onPreviewPortColour is skipped when unset (the default in a standalone test).
                    if (auto* dialog = safeDialog.getComponent())
                        if (dialog->onPreviewPortColour)
                            dialog->onPreviewPortColour(uuid, c);
                }
            },
            [safeDialog, uuid](juce::Colour c) {
                if (auto* dialog = safeDialog.getComponent())
                    if (dialog->onChangePortColour)
                        dialog->onChangePortColour(uuid, c);
            });
    }

    void showColourPicker() {
        juce::CallOutBox::launchAsynchronously(buildColourPicker(), colourSwatch.getScreenBounds(), nullptr);
    }

    // The real drag-end path (DragHandle::onDragEnd -> MacroPortConfigDialog::endRowDrag) and
    // dragRowToIndexInGroupForTest both call this — one place that turns "a target index" into
    // the onReorderPortTo callback.
    void commitDragTo(int newIndexInGroup) {
        if (owner_.onReorderPortTo)
            owner_.onReorderPortTo(nodeUuid, newIndexInGroup);
    }

    // Whether this row currently carries a user-set colour (as opposed to just displaying the
    // kind-tint fallback) — read by getRowHasCustomColourForTest rather than the dialog's own
    // `rows_` snapshot, which is only refreshed on the next refreshPorts() round trip and would
    // otherwise read stale immediately after a same-tick commitColour().
    bool hasCustomColourForTest() const { return customColour.has_value(); }

    // 0..1: how strongly the row is drawn lifted while it is dragged. Purely visual — repaints only
    // on an actual change, matching Source/UI/CLAUDE.md's "no unconditional repaint" rule.
    void setLift(float lift) {
        if (lift_ != lift) {
            lift_ = lift;
            repaint();
        }
    }

    bool isPolyShown() const {
        return !isMidi && shapeFromComboIndex(shapeBox.getSelectedId()) == MacroPortShape::Poly;
    }

    // The voices editor fades in and out as the shape changes; shapeBox slides over as its block's width follows
    // the fade (resized()).
    void updateVoicesVisibility() {
        voicesFade_.setShown(isPolyShown());
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

    // The close-time analogue of maybeCommitShape(), for the voices field only — NOT a
    // blanket "call maybeCommitShape() for every row on close." Re-deriving "the current shape"
    // from shapeBox at close time is unsafe for a StereoCollapsed row: the combo has no item id
    // of its own for StereoCollapsed (comboIndexFromShape maps it to kShapeStereoId, the same id
    // Stereo uses — see that function's comment), so shapeFromComboIndex(shapeBox.getSelectedId())
    // reads back plain Stereo for a collapsed row even when the user never touched the combo.
    // Calling maybeCommitShape() unconditionally here would silently convert every untouched
    // StereoCollapsed port into a real two-jack Stereo port (a GraphEditor::changeMacroPortShape
    // delete+recreate, complete with a fresh uuid and dropped cables) just from opening and
    // closing the dialog. Voices only matters while the row is showing Poly (the only shape that
    // makes voicesEditor visible at all, and Poly can never be the lossy StereoCollapsed case), so
    // gating on the voices editor being shown (voicesFade_.isShown(): false the moment its fade-out starts, not
    // when it ends) keeps this reachable only through the one path that's actually safe to
    // re-derive from the combo's current selection.
    void maybeCommitVoicesOnClose() {
        if (voicesFade_.isShown())
            maybeCommitShape();
    }

    // Mirrors maybeCommitShape()'s exact guard style: never fires onRenamePort twice for the same
    // text (once from a real edit, once more if requestClose() below also calls this on a row
    // nothing changed on) — the guard is what makes calling this unconditionally from
    // requestClose() for every row safe.
    void maybeCommitName() {
        const auto newName = nameEditor.getText();
        if (newName == committedName_)
            return;
        committedName_ = newName;
        if (owner_.onRenamePort)
            owner_.onRenamePort(nodeUuid, newName);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(6, 3);

        auto placeGlyph = [&](GlyphButton& btn) {
            btn.setBounds(
                area.removeFromRight(kGlyphButtonSize).withSizeKeepingCentre(kGlyphButtonSize, kGlyphButtonSize));
            area.removeFromRight(kGlyphButtonGap);
        };
        placeGlyph(deleteButton);
        area.removeFromRight(8);

        if (isMidi) {
            midiTag.setBounds(area.removeFromRight(56));
        } else {
            // The voices block (editor, label, gap: 90 px) is as wide as the fade has got, so the shape box slides.
            if (const int voicesWidth = juce::roundToInt(90.0f * voicesFade_.progress()); voicesWidth > 0) {
                auto block = area.removeFromRight(voicesWidth);
                voicesEditor.setBounds(block.removeFromRight(38));
                block.removeFromRight(4);
                voicesLabel.setBounds(block.removeFromRight(42));
            }
            shapeBox.setBounds(area.removeFromRight(90));
        }
        area.removeFromRight(8);

        // Colour swatch, then the drag handle, on the left edge (paint() below draws no
        // separate kind-tint bar).
        colourSwatch.setBounds(
            area.removeFromLeft(kColourSwatchSize).withSizeKeepingCentre(kColourSwatchSize, kColourSwatchSize));
        area.removeFromLeft(4);
        dragHandle.setBounds(area.removeFromLeft(kDragHandleWidth));
        area.removeFromLeft(6);

        nameEditor.setBounds(area);
    }

    void paint(juce::Graphics& g) override {
        const auto& c = synth::theme::themeOf(*this).colors;
        auto bounds = getLocalBounds().toFloat();

        paintReorderLift(g, bounds, lift_, c.surfaceHi, c.accent);
        if (isMidi) {
            g.setColour(c.midiWire.withAlpha(0.08f));
            g.fillRoundedRectangle(bounds, 5.0f);
        }
    }

    juce::String nodeUuid;
    bool isMidi = false;

    juce::TextEditor nameEditor;
    juce::Label midiTag{"midiTag", juce::String()};
    juce::ComboBox shapeBox; // AudioCV rows only; hidden entirely for a MIDI row
    juce::Label voicesLabel{"voicesLabel", juce::String()};
    juce::TextEditor voicesEditor; // shown only while shapeBox reads Poly-N
    PortColourSwatch colourSwatch;
    DragHandle dragHandle;
    GlyphButton deleteButton;

private:
    MacroPortConfigDialog& owner_; // outlives this row: owned by owner_.rowControls_
    MacroPortShape committedShape_;
    int committedVoices_;
    juce::String committedName_;              // see maybeCommitName()
    std::optional<juce::Colour> customColour; // nullopt = falls back to kindTintColour()
    float lift_ = 0.0f;                       // see setLift()

    // The voices editor and its label, shown for a poly shape only. Declared after them: destroyed first.
    synth::ui::FadeVisibility voicesFade_{&voicesLabel, &voicesEditor};
};

} // namespace synth::ui
