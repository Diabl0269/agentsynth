#pragma once

// Shared between the split MacroPortConfigDialog*.cpp units: the "Add a port" combo-box item ids
// (constructed in Lifecycle.cpp, read back in TestSeams.cpp), liveThemeColours (used by the row
// widgets below and by MacroPortConfigDialog::paint in Lifecycle.cpp), and — because it is a
// private nested class of MacroPortConfigDialog whose members Lifecycle.cpp (rebuildRowComponents),
// RowOrdering.cpp (drag/keyboard nav), and TestSeams.cpp all need the complete type for — the row
// widgets (GlyphButton, PortColourSwatch, DragHandle) and PortRowComponent itself.

#include "MacroPortConfigDialog.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
// ComboBox item ids are 1-based in JUCE.
constexpr int kDirectionInputId = 1;
constexpr int kDirectionOutputId = 2;

constexpr int kKindAudioCVId = 1;
constexpr int kKindMidiId = 2;
} // namespace

// Resolves the live theme's colour tokens, evaluated fresh at every call site (never cached) so a
// theme switch or this dialog being reparented mid-life is never stale — the same reasoning
// PreferencesSettingsTab's popup content and MidiDestinationPicker give for the identical
// dynamic_cast, and why it is done here at PAINT time rather than once at construction (a
// juce::DialogWindow's content component is not guaranteed to already sit under
// synth::theme::AppLookAndFeel the moment its constructor runs).
inline const synth::theme::Colors& liveThemeColours(const juce::Component& c) {
    static const synth::theme::Colors fallback{};
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&c.getLookAndFeel()))
        return lf->getTheme().colors;
    return fallback;
}

namespace {
// T153: Up/Down on a control that opts in (the row's colour swatch and Delete glyph button — see
// MacroPortConfigDialog::moveRowFocus's comment for why arrow navigation is scoped to only those)
// reports itself as an arrow key rather than the caller re-deriving KeyPress comparisons at every
// call site. GlyphButton/PortColourSwatch::keyPressed also check the command modifier themselves
// (founder review round 4's Cmd+Up/Cmd+Down reorder chord) before falling through to this.
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
constexpr int kColourSwatchSize = 16; // T152
constexpr int kDragHandleWidth = 14;  // T152

// Same live/fallback split as liveThemeColours, for the border-width metric the focus-ring paint
// below needs (founder review round 4) — kept as a separate accessor rather than widening
// liveThemeColours's return type, since every existing call site only ever wanted colours.
inline const synth::theme::Metrics& liveThemeMetrics(const juce::Component& c) {
    static const synth::theme::Metrics fallback{};
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&c.getLookAndFeel()))
        return lf->getTheme().metrics;
    return fallback;
}

// A compact icon-style affordance replacing the old full-width "Delete" text button (founder
// review item 1) — a small square button drawing an X as a couple of strokes, the same "drawn
// Path/lines, not an SVG asset" idiom MacroCardComponent's own expand chevron already uses. Used
// to also draw Up/Down triangles for the per-row reorder buttons removed in founder review round
// 4 (Cmd+Up/Cmd+Down on this same button is the keyboard-accessible replacement — see
// PortRowComponent's onReorderChord wiring below); Delete is the only glyph left.
class GlyphButton : public juce::Button {
public:
    enum class Glyph { Delete };

    explicit GlyphButton(Glyph glyph)
        : juce::Button(juce::String())
        , glyph_(glyph) {}

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        juce::ignoreUnused(glyph_); // only one glyph remains; kept for a future affordance to reuse
        const auto& c = liveThemeColours(*this);
        const juce::Colour hotColour = c.error;
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
        g.drawLine(inner.getX(), inner.getY(), inner.getRight(), inner.getBottom(), 1.6f);
        g.drawLine(inner.getX(), inner.getBottom(), inner.getRight(), inner.getY(), 1.6f);

        // Founder review round 4: keyboard-focus indicator. juce::Button::paint() only ever hands
        // paintButton() isOver()/isDown() (juce_Button.cpp), never keyboard-focus state, so a
        // Tab'd-to-but-not-hovered button painted with no visible change at all — confirmed by
        // reading Button::paint()'s call site rather than assumed. Reuses AppLookAndFeel::
        // drawTextEditorOutline/drawComboBox's own "accent outline when focused" convention.
        if (hasKeyboardFocus(true) || forceFocusRingForTest) {
            const auto& m = liveThemeMetrics(*this);
            g.setColour(c.accent);
            g.drawRoundedRectangle(bounds.reduced(m.borderWidth * 0.5f), 4.0f, m.borderWidth);
        }
    }

    // T153: the Delete button is the keyboard-accessible delete fallback (it already gets Tab/
    // Return/Space for free from juce::Button) — this adds Up/Down-arrow FOCUS navigation between
    // rows on top, wired by PortRowComponent to MacroPortConfigDialog::moveRowFocus. Founder
    // review round 4: Cmd+Up/Cmd+Down is checked FIRST and fires onReorderChord instead (the
    // keyboard-accessible replacement for the removed per-row Up/Down buttons) — a bare arrow
    // still only moves focus, since that's already its established meaning here. Returning false
    // when nothing is wired (or the key isn't an arrow) falls through to Button::keyPressed so
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
        return juce::Button::keyPressed(key);
    }

    std::function<void(bool moveDown)> onVerticalArrow;
    std::function<void(bool moveDown)> onReorderChord; // founder review round 4 (Cmd+Up/Cmd+Down)
    bool forceFocusRingForTest = false;                // founder review round 4 test seam

private:
    Glyph glyph_;
};

// T152: the row's kind-tinted left-edge bar is now a real clickable swatch (founder review round
// 3, item 3.4) instead of a plain painted rectangle — left-click opens a synth::ui::
// ColourPickerPopup (the same favourites-shelf picker TimelineTrackHeaderComponent's track colour
// swatch and AppearanceSettingsTab's note swatches already use), right-click resets to the
// kind-tint default. `colour` is what actually PAINTS (custom colour, or the kind tint fallback);
// PortRowComponent is the one that decides which of those it currently is.
class PortColourSwatch : public juce::Button {
public:
    PortColourSwatch()
        : juce::Button("portColourSwatch") {}

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(colour);
        g.fillRoundedRectangle(bounds, 3.0f);
        const auto& c = liveThemeColours(*this);
        // Founder review round 4: same "no visible focus state" bug as GlyphButton (see its own
        // comment for the confirmed root cause) — accent replaces the normal border colour when
        // focused, matching AppLookAndFeel's own "accent when focused" convention.
        const bool focused = hasKeyboardFocus(true) || forceFocusRingForTest;
        g.setColour(focused ? c.accent : c.border.withAlpha(highlighted || down ? 0.9f : 0.45f));
        g.drawRoundedRectangle(bounds, 3.0f,
                               focused ? liveThemeMetrics(*this).borderWidth : (highlighted || down ? 1.4f : 1.0f));
    }

    // Mirrors ColourPickerPopup::FavouriteSwatchButton's own override exactly (see its comment):
    // a right-click must reset to default, not ALSO fire onClick the way a plain Button would.
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) {
            if (onRightClick)
                onRightClick();
            return;
        }
        juce::Button::mouseDown(e);
    }

    // Founder review round 4: Cmd+Up/Cmd+Down is checked first and fires onReorderChord (the
    // keyboard-accessible replacement for the removed per-row Up/Down buttons); a bare arrow
    // still only moves focus via onVerticalArrow, its pre-existing meaning — see GlyphButton's
    // identical override for the full reasoning.
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
        return juce::Button::keyPressed(key);
    }

    juce::Colour colour{juce::Colours::grey};
    std::function<void()> onRightClick;
    std::function<void(bool moveDown)> onVerticalArrow;
    std::function<void(bool moveDown)> onReorderChord; // founder review round 4 (Cmd+Up/Cmd+Down)
    bool forceFocusRingForTest = false;                // founder review round 4 test seam
};

// T152: the drag-to-reorder handle — a small grip icon to the left of the name editor. Deliberately
// a plain juce::Component, not a juce::Button: dragging is mouse-only by design (Cmd+Up/Cmd+Down on
// the colour swatch or Delete button is the keyboard-accessible fallback, founder review round 4,
// so this handle never needs to be a tab stop), and a plain Component sidesteps Button's own
// click-vs-drag heuristics entirely rather than fighting them.
class DragHandle
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    void paint(juce::Graphics& g) override {
        const auto& c = liveThemeColours(*this);
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

    void mouseDown(const juce::MouseEvent&) override {
        dragging_ = true;
        repaint();
        if (onDragStart)
            onDragStart();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (onDragMove)
            onDragMove(e.getScreenPosition());
    }

    void mouseUp(const juce::MouseEvent&) override {
        dragging_ = false;
        repaint();
        if (onDragEnd)
            onDragEnd();
    }

    std::function<void()> onDragStart;
    std::function<void(juce::Point<int> screenPos)> onDragMove;
    std::function<void()> onDragEnd;

private:
    bool dragging_ = false;
};
} // namespace

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
        // T153: Escape closes the WHOLE modal (docs/macros/configure-io.md#keyboard-handling decision on this — see the
        // class comment) rather than just reverting this field's edit, matching Close's own behaviour exactly (a
        // focus-loss side effect during teardown commits whatever text is here, the same as clicking Close already does
        // — Escape does not discard anything Close wouldn't). Wired here (rather than relying on the bubble
        // MacroPortConfigDialog::keyPressed catches) because juce::TextEditor consumes Escape itself before it ever
        // bubbles.
        nameEditor.onEscapeKey = [this] { owner_.requestClose(); };
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
        // (docs/macros/ports.md#port-set-and-ordering) that is built entirely on uuid identity.
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
        voicesEditor.onEscapeKey = [this] { // same T153 reasoning as nameEditor's onEscapeKey above
            owner_.requestClose();
        };
        addAndMakeVisible(voicesEditor);

        // T152: the per-port colour swatch — left-click opens a ColourPickerPopup, right-click
        // resets to the kind-tint default. Starts from whatever the row was constructed with
        // (nullopt for every pre-T152 port), never anything the dialog invents.
        customColour = row.colour;
        colourSwatch.colour = customColour.value_or(kindTintColour());
        colourSwatch.setTooltip("Port colour (right-click to reset)");
        colourSwatch.onClick = [this] { showColourPicker(); };
        colourSwatch.onRightClick = [this] { commitColour(std::nullopt); };
        colourSwatch.onVerticalArrow = [this](bool moveDown) {
            owner_.moveRowFocus(*this, MacroPortConfigDialog::RowControl::Colour, moveDown);
        };
        // Founder review round 4: Cmd+Up/Cmd+Down on the colour swatch reorders this port — the
        // keyboard-accessible replacement for the removed per-row Up/Down buttons.
        colourSwatch.onReorderChord = [this](bool moveDown) {
            if (owner_.onReorderPort)
                owner_.onReorderPort(nodeUuid, /*moveUp=*/!moveDown);
        };
        addAndMakeVisible(colourSwatch);

        // T152: the drag-to-reorder handle. Cmd+Up/Cmd+Down on the colour swatch or Delete button
        // below stays fully functional as the keyboard fallback (founder review round 4) — this
        // is an ADDITIONAL, mouse-only gesture, never a replacement.
        dragHandle.setTooltip("Drag to reorder (or Cmd+Up/Cmd+Down on a focused control)");
        dragHandle.onDragStart = [this] { owner_.beginRowDrag(*this); };
        dragHandle.onDragMove = [this](juce::Point<int> screenPos) { owner_.updateRowDrag(*this, screenPos); };
        dragHandle.onDragEnd = [this] { owner_.endRowDrag(*this); };
        addAndMakeVisible(dragHandle);

        deleteButton.setTooltip("Delete this port (Cmd+Up/Cmd+Down to reorder)");
        deleteButton.onClick = [this] {
            if (owner_.onDeletePort)
                owner_.onDeletePort(nodeUuid);
        };
        deleteButton.onVerticalArrow = [this](bool moveDown) {
            owner_.moveRowFocus(*this, MacroPortConfigDialog::RowControl::Delete, moveDown);
        };
        // Founder review round 4: Cmd+Up/Cmd+Down on the delete button reorders this port — the
        // keyboard-accessible replacement for the removed per-row Up/Down buttons.
        deleteButton.onReorderChord = [this](bool moveDown) {
            if (owner_.onReorderPort)
                owner_.onReorderPort(nodeUuid, /*moveUp=*/!moveDown);
        };
        addAndMakeVisible(deleteButton);

        updateVoicesVisibility();
    }

    juce::Colour kindTintColour() const {
        const auto& c = liveThemeColours(*this);
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
            [safeRow](juce::Colour c) {
                if (auto* row = safeRow.getComponent()) {
                    row->colourSwatch.colour = c;
                    row->colourSwatch.repaint();
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

    // -1 = none, 0 = insertion point is ABOVE this row, 1 = BELOW it. Purely visual — repaints
    // only on an actual change, matching Source/UI/CLAUDE.md's "no unconditional repaint" rule.
    void setDropIndicator(int position) {
        if (dropIndicatorPosition_ != position) {
            dropIndicatorPosition_ = position;
            repaint();
        }
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

    // T150: the close-time analogue of maybeCommitShape(), for the voices field only — NOT a
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
    // gating on isVisible() keeps this reachable only through the one path that's actually safe to
    // re-derive from the combo's current selection.
    void maybeCommitVoicesOnClose() {
        if (voicesEditor.isVisible())
            maybeCommitShape();
    }

    // T150: mirrors maybeCommitShape()'s exact guard style. Rename previously fired
    // owner_.onRenamePort unconditionally from onFocusLost/onReturnKey, with no protection
    // against firing twice for the same text (once from a real edit, once more if requestClose()
    // below also calls this on a row nothing changed on) — the guard here is what makes calling
    // this unconditionally from requestClose() for every row safe.
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
            if (voicesEditor.isVisible()) {
                voicesEditor.setBounds(area.removeFromRight(38));
                area.removeFromRight(4);
                voicesLabel.setBounds(area.removeFromRight(42));
                area.removeFromRight(6);
            }
            shapeBox.setBounds(area.removeFromRight(90));
        }
        area.removeFromRight(8);

        // T152: colour swatch, then the drag handle, on the left — replacing the old painted
        // kind-tint bar that used to occupy this same edge (paint() below no longer draws it).
        colourSwatch.setBounds(
            area.removeFromLeft(kColourSwatchSize).withSizeKeepingCentre(kColourSwatchSize, kColourSwatchSize));
        area.removeFromLeft(4);
        dragHandle.setBounds(area.removeFromLeft(kDragHandleWidth));
        area.removeFromLeft(6);

        nameEditor.setBounds(area);
    }

    void paint(juce::Graphics& g) override {
        const auto& c = liveThemeColours(*this);
        auto bounds = getLocalBounds().toFloat();

        if (isMidi) {
            g.setColour(c.midiWire.withAlpha(0.08f));
            g.fillRoundedRectangle(bounds, 5.0f);
        }

        // T152: drag insertion indicator — a thin accent line at the edge the dragged row would
        // land next to. Drawn here (not by MacroPortConfigDialog/rowsContent_) so it always tracks
        // this row's own live bounds with no separate geometry computation to drift out of sync.
        if (dropIndicatorPosition_ >= 0) {
            g.setColour(c.accent);
            auto local = getLocalBounds();
            g.fillRect(dropIndicatorPosition_ == 0 ? local.removeFromTop(2) : local.removeFromBottom(2));
        }
    }

    juce::String nodeUuid;
    bool isMidi = false;

    juce::TextEditor nameEditor;
    juce::Label midiTag{"midiTag", juce::String()};
    juce::ComboBox shapeBox; // AudioCV rows only; hidden entirely for a MIDI row
    juce::Label voicesLabel{"voicesLabel", juce::String()};
    juce::TextEditor voicesEditor; // shown only while shapeBox reads Poly-N
    PortColourSwatch colourSwatch; // T152
    DragHandle dragHandle;         // T152
    GlyphButton deleteButton;

private:
    MacroPortConfigDialog& owner_; // outlives this row: owned by owner_.rowControls_
    MacroPortShape committedShape_;
    int committedVoices_;
    juce::String committedName_;              // T150; see maybeCommitName()
    std::optional<juce::Colour> customColour; // T152; nullopt = falls back to kindTintColour()
    int dropIndicatorPosition_ = -1;          // T152; -1 none, 0 above, 1 below
};

} // namespace synth::ui
