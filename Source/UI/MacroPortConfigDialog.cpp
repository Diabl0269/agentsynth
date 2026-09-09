#include "MacroPortConfigDialog.h"
#include "Theme/AppLookAndFeel.h"

namespace synth::ui {

namespace {
// ComboBox item ids are 1-based in JUCE.
constexpr int kDirectionInputId = 1;
constexpr int kDirectionOutputId = 2;

// T153: Up/Down on a control that opts in (the row's colour swatch and Delete glyph button — see
// MacroPortConfigDialog::moveRowFocus's comment for why arrow navigation is scoped to only those)
// reports itself as an arrow key rather than the caller re-deriving KeyPress comparisons at every
// call site. GlyphButton/PortColourSwatch::keyPressed also check the command modifier themselves
// (founder review round 4's Cmd+Up/Cmd+Down reorder chord) before falling through to this.
bool isVerticalArrowKey(const juce::KeyPress& key, bool& outMoveDown) {
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

constexpr int kKindAudioCVId = 1;
constexpr int kKindMidiId = 2;

constexpr int kShapeMonoId = 1;
constexpr int kShapeStereoId = 2;
constexpr int kShapePolyId = 3;

constexpr int kGlyphButtonSize = 20;
constexpr int kGlyphButtonGap = 2;
constexpr int kColourSwatchSize = 16; // T152
constexpr int kDragHandleWidth = 14;  // T152

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

// Same live/fallback split as liveThemeColours, for the border-width metric the focus-ring paint
// below needs (founder review round 4) — kept as a separate accessor rather than widening
// liveThemeColours's return type, since every existing call site only ever wanted colours.
const synth::theme::Metrics& liveThemeMetrics(const juce::Component& c) {
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
        // T153: Escape closes the WHOLE modal (docs/macros.md's decision on this — see the class
        // comment) rather than just reverting this field's edit, matching Close's own behaviour
        // exactly (a focus-loss side effect during teardown commits whatever text is here, the
        // same as clicking Close already does — Escape does not discard anything Close wouldn't).
        // Wired here (rather than relying on the bubble MacroPortConfigDialog::keyPressed catches)
        // because juce::TextEditor consumes Escape itself before it ever bubbles.
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

// ---- T152 drag-to-reorder ----------------------------------------------------------------------
// beginRowDrag/updateRowDrag/endRowDrag implement the real mouse path (DragHandle wires straight
// to these); the *ForTest seams below call PortRowComponent::commitDragTo directly instead of
// synthesizing a mouseDown/mouseDrag/mouseUp sequence, the same "drive the real controls, skip the
// mouse plumbing" idiom every other *ForTest seam in this file already uses.

void MacroPortConfigDialog::beginRowDrag(PortRowComponent& row) {
    draggingNodeUuid_ = row.nodeUuid;
    dragDropIndexInGroup_ = -1; // recomputed on the first updateRowDrag; -1 = "no move yet"
}

void MacroPortConfigDialog::updateRowDrag(PortRowComponent& row, juce::Point<int> screenPos) {
    if (row.nodeUuid != draggingNodeUuid_)
        return; // defensive: only the row that started the drag drives it

    const int draggedIndex = rowControls_.indexOf(&row);
    if (draggedIndex < 0)
        return;
    const bool isInput = rows_[(size_t)draggedIndex].isInput;
    const int localY = rowsContent_.getLocalPoint(nullptr, screenPos).y;

    // Count how many OTHER rows in the same direction group sit above the drop point — that count
    // IS the dragged row's new index once it is removed from and reinserted into that group,
    // exactly the index reorderMacroPortToIndex (GraphEditor.cpp) expects. Excluding the dragged
    // row itself (rather than comparing against its own, unmoving on-screen position) is what
    // makes this work with the row staying visually in place during the drag, instead of needing
    // to follow the cursor like a real "lift and carry" drag would.
    int dropIndex = 0;
    int firstOtherInGroup = -1, lastOtherInGroup = -1;
    for (int i = 0; i < rowControls_.size(); ++i) {
        if (i == draggedIndex || rows_[(size_t)i].isInput != isInput)
            continue;
        if (firstOtherInGroup < 0)
            firstOtherInGroup = i;
        lastOtherInGroup = i;
        if (localY > rowControls_[i]->getBounds().getCentreY())
            ++dropIndex;
    }
    dragDropIndexInGroup_ = dropIndex;

    // Visual feedback: the two rows straddling the drop point get an insertion-line indicator;
    // every other row (including the dragged one) clears it. Computed fresh each call rather than
    // diffed against the previous call — setDropIndicator() itself is the repaint-only-on-change
    // guard, so this stays cheap.
    for (int i = 0; i < rowControls_.size(); ++i) {
        if (i == draggedIndex || rows_[(size_t)i].isInput != isInput) {
            rowControls_[i]->setDropIndicator(-1);
            continue;
        }
        int otherRank = 0; // this row's rank among the OTHER rows in its group, top to bottom
        for (int j = firstOtherInGroup; j <= lastOtherInGroup; ++j) {
            if (j == draggedIndex || rows_[(size_t)j].isInput != isInput)
                continue;
            if (j == i)
                break;
            ++otherRank;
        }
        if (otherRank == dropIndex)
            rowControls_[i]->setDropIndicator(0); // the drop lands just above this row
        else if (otherRank == dropIndex - 1)
            rowControls_[i]->setDropIndicator(1); // the drop lands just below this row
        else
            rowControls_[i]->setDropIndicator(-1);
    }
}

void MacroPortConfigDialog::endRowDrag(PortRowComponent& row) {
    clearDragIndicators();
    if (row.nodeUuid == draggingNodeUuid_ && dragDropIndexInGroup_ >= 0)
        row.commitDragTo(dragDropIndexInGroup_);
    draggingNodeUuid_ = {};
    dragDropIndexInGroup_ = -1;
}

void MacroPortConfigDialog::clearDragIndicators() {
    for (auto* rc : rowControls_)
        rc->setDropIndicator(-1);
}

// ---- T153 keyboard row navigation ---------------------------------------------------------------

int MacroPortConfigDialog::arrowNavigationTargetRow(int fromRow, bool moveDown) const {
    const int target = fromRow + (moveDown ? 1 : -1);
    return (target >= 0 && target < rowControls_.size()) ? target : -1;
}

void MacroPortConfigDialog::moveRowFocus(PortRowComponent& from, RowControl target, bool moveDown) {
    const int fromIndex = rowControls_.indexOf(&from);
    if (fromIndex < 0)
        return;
    const int toIndex = arrowNavigationTargetRow(fromIndex, moveDown);
    if (toIndex < 0)
        return; // at either end of the list — no wraparound (see the header's own comment)

    auto* target_ = rowControls_[toIndex];
    switch (target) {
    case RowControl::Colour:
        target_->colourSwatch.grabKeyboardFocus();
        break;
    case RowControl::Delete:
        target_->deleteButton.grabKeyboardFocus();
        break;
    }
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

// ---- T152 test seams: drag-to-reorder + per-port colour -----------------------------------

void MacroPortConfigDialog::dragRowToIndexInGroupForTest(int row, int newIndexInGroup) {
    if (row >= 0 && row < (int)rowControls_.size())
        rowControls_[row]->commitDragTo(newIndexInGroup);
}

juce::Colour MacroPortConfigDialog::getRowDisplayColourForTest(int row) const {
    return (row >= 0 && row < (int)rowControls_.size()) ? rowControls_[row]->colourSwatch.colour
                                                        : juce::Colours::transparentBlack;
}

bool MacroPortConfigDialog::getRowHasCustomColourForTest(int row) const {
    return (row >= 0 && row < (int)rowControls_.size()) && rowControls_[row]->hasCustomColourForTest();
}

void MacroPortConfigDialog::setRowColourForTest(int row, juce::Colour colour) {
    if (row >= 0 && row < (int)rowControls_.size())
        rowControls_[row]->commitColour(colour);
}

void MacroPortConfigDialog::resetRowColourForTest(int row) {
    if (row >= 0 && row < (int)rowControls_.size())
        rowControls_[row]->commitColour(std::nullopt);
}

std::unique_ptr<synth::ui::ColourPickerPopup> MacroPortConfigDialog::createRowColourPickerForTest(int row) {
    if (row < 0 || row >= (int)rowControls_.size())
        return nullptr;
    return rowControls_[row]->buildColourPicker();
}

// ---- T153 test seams: keyboard accessibility -----------------------------------------------

void MacroPortConfigDialog::simulateRowNameEscapeForTest(int row) {
    if (row >= 0 && row < (int)rowControls_.size() && rowControls_[row]->nameEditor.onEscapeKey)
        rowControls_[row]->nameEditor.onEscapeKey();
}

void MacroPortConfigDialog::simulateNewPortNameEscapeForTest() {
    if (newNameEditor_.onEscapeKey)
        newNameEditor_.onEscapeKey();
}

void MacroPortConfigDialog::simulateNewPortNameReturnForTest() {
    if (newNameEditor_.onReturnKey)
        newNameEditor_.onReturnKey();
}

int MacroPortConfigDialog::computeArrowNavigationTargetRowForTest(int fromRow, bool moveDown) const {
    return arrowNavigationTargetRow(fromRow, moveDown);
}

bool MacroPortConfigDialog::simulateRowControlArrowKeyForTest(int row, RowControl control, bool moveDown,
                                                              bool withCommandModifier) {
    if (row < 0 || row >= (int)rowControls_.size())
        return false;
    const auto mods =
        withCommandModifier ? juce::ModifierKeys(juce::ModifierKeys::commandModifier) : juce::ModifierKeys();
    const auto key = juce::KeyPress(moveDown ? juce::KeyPress::downKey : juce::KeyPress::upKey, mods, 0);
    auto* rc = rowControls_[row];
    switch (control) {
    case RowControl::Colour:
        return rc->colourSwatch.keyPressed(key);
    case RowControl::Delete:
        return rc->deleteButton.keyPressed(key);
    }
    return false;
}

// ---- Founder review round 4 test seams: focus-visibility regression coverage ------------------

void MacroPortConfigDialog::setRowFocusRingForcedForTest(int row, bool forced) {
    if (row < 0 || row >= (int)rowControls_.size())
        return;
    rowControls_[row]->deleteButton.forceFocusRingForTest = forced;
    rowControls_[row]->deleteButton.repaint();
}

juce::Image MacroPortConfigDialog::renderRowDeleteButtonForTest(int row) const {
    if (row < 0 || row >= (int)rowControls_.size())
        return {};
    auto& btn = rowControls_[row]->deleteButton;
    return btn.createComponentSnapshot(btn.getLocalBounds());
}

juce::Rectangle<int> MacroPortConfigDialog::getAddButtonBoundsForTest() const { return addButton_.getBounds(); }

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

// T153: see the class comment for the decision — Escape == "Leave Cables As Is", remember forced
// to false regardless of the toggle's current state.
bool MacroAutoPortPromptDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (onChoice)
            onChoice(/*createPorts=*/false, /*remember=*/false);
        return true;
    }
    return false;
}

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
