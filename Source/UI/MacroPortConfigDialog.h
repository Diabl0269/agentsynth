#pragma once

#include "../MacroSet.h"
#include "../Modules/MacroPortShape.h"
#include "ColourPickerPopup.h" // juce::PropertiesFile (juce_data_structures) + ColourPickerPopup itself (T152)
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <vector>

namespace synth::ui {

/**
 * @brief The "Configure I/O" modal for one Macro (P8-15b, T140; redesigned in the F1 founder-review
 * fix pass).
 *
 * Unifies docs/macros.md §7 items 3 ("Add Input"/"Add Output") and 5 (rename/reorder) into ONE
 * small modal, per an explicit founder request rather than piecemeal menu actions: add/remove/
 * rename/reorder every input and output on the macro from one place, picking Mono/Stereo/Poly-N/
 * MIDI at creation time (§5.3 — a port's shape/kind is fixed once created; changing shape means
 * deleting the port and adding a new one, which `onChangePortShape` below asks the OWNER to do as
 * ONE undo step, not a delete followed by a separately-undoable add).
 *
 * Pure UI, exactly like ExportAudioDialog: it holds no graph or synth::MacroSet reference of its
 * own, only a snapshot of PortRow data the caller hands in, and emits intent callbacks the caller
 * executes against the live macro+graph (GraphEditor::promptConfigureMacroIO). That split is what
 * makes this headless-testable — a test constructs it with fixed rows, drives the real controls,
 * and reads back what each gesture would send, with no AudioEngine/GraphEditor involved at all.
 *
 * F1 redesign (founder review item 1): rows group under "Inputs"/"Outputs" section headers, a
 * MIDI row hides its shape controls entirely rather than showing them disabled, the poly voice
 * count carries a "Voices" label and only appears when the shape is Poly, Up/Down/Delete are
 * compact glyph buttons instead of three full-width text buttons, changing a port's shape commits
 * the moment the combo box (or, for voice count, the number field) changes rather than needing a
 * separate "Apply Shape" click, and the dialog sizes itself to its content (clamped, with the row
 * list scrolling past the clamp) instead of a fixed 800px box with dead space below the last row.
 *
 * Founder review round 3 (T152/item 3.3, T152/item 3.4): a row is now also drag-reorderable (the
 * Up/Down glyph buttons stayed as the keyboard-accessible fallback at the time — see the round 4
 * note below for why they're gone now), and each row's kind-tinted left-edge bar is now a real
 * clickable swatch that opens a colour picker to set a per-port colour (`onChangePortColour`),
 * right-click resets to the kind-tint default. Both gestures are structurally confined to one
 * direction (inputs against inputs, outputs against outputs) the same way `onReorderPort` already
 * was: `onReorderPortTo`'s index is scoped to the dragged row's OWN direction group, so there is
 * no way to express "become an output" through it.
 *
 * Founder review round 3 (T153): keyboard accessibility. Tab order follows JUCE's default
 * top-to-bottom/left-to-right traversal (every real control here already `setWantsKeyboardFocus`s
 * by default — Button/ComboBox/TextEditor all do), Return commits whichever text field currently
 * has focus (unchanged — already true for rename/voices, and now also the "Add a port" name
 * field), and Escape closes the dialog via the SAME path the Close button uses
 * (`onRequestClose`) — including committing whatever rename/shape edit currently has focus, since
 * that is a pre-existing side effect of losing focus during teardown, not something Escape does
 * differently from Close. Arrow-Up/Down on a row's colour swatch or Delete button moves keyboard
 * focus to the same control on the row above/below (never wraps), which does not conflict with a
 * ComboBox's or TextEditor's own arrow-key handling since those controls are not where this is
 * wired.
 *
 * Founder review round 4 (real-build testing of T152/T153): three fixes.
 * (1) The per-row Up/Down glyph buttons are REMOVED — now that drag-to-reorder is confirmed
 * working, they were pure visual clutter. Keyboard-accessible reordering (the whole point of
 * T153) survives as a Cmd+Up/Cmd+Down chord on the row's remaining controls (colour swatch,
 * Delete button) — `GlyphButton`/`PortColourSwatch::keyPressed` check the command modifier BEFORE
 * falling through to the bare-arrow row-navigation check above, so a bare arrow still only moves
 * focus (it already means that) and Cmd+arrow is the new reorder trigger, matching the app's
 * existing convention of the command modifier for editing-type actions (Cmd+D duplicate, Cmd+R
 * repeat — docs/shortcuts.md). This is dialog-local key handling, not a ShortcutManager action.
 * (2) Keyboard focus was invisible everywhere in this dialog: `juce::Button::paint()` passes
 * `paintButton` only `isOver()`/`isDown()` (juce_Button.cpp), never keyboard-focus state, so the
 * custom `GlyphButton`/`PortColourSwatch` classes never drew anything different when focused.
 * Both now check `hasKeyboardFocus(true)` and draw an accent outline, reusing
 * `AppLookAndFeel::drawTextEditorOutline`/`drawComboBox`'s own "accent when focused" convention;
 * `AppLookAndFeel::drawButtonBackground` got the same treatment for every plain `juce::TextButton`
 * in the app (including this dialog's Add/Close buttons), which had no focus indication either
 * (`LookAndFeel_V4`'s default draws none). Every repaint here is the free one JUCE's own
 * `Button::focusGained`/`focusLost` already trigger — no timer, per Source/UI/CLAUDE.md.
 * (3) The "Add a port" panel's Add button/name field were invisible: `resized()`'s
 * `addBlockArea` budgeted height for only 2 of the panel's 3 rows (label + newRow1), so newRow2
 * (name field + Add button) was squeezed to zero height. Fixed by computing the panel's height
 * from all 3 rows in `kAddBlockHeight`, used by both `resized()` and `idealDialogHeight()` so
 * they can never drift apart again (the same reasoning `layOutOrMeasureRows` already documents
 * for the row list).
 */
class MacroPortConfigDialog : public juce::Component {
public:
    struct PortRow {
        juce::String nodeUuid;
        bool isInput = false;
        juce::String name;
        synth::MacroPortKind kind = synth::MacroPortKind::AudioCV;
        MacroPortShape shape = MacroPortShape::Mono; // meaningless when kind == Midi
        int voiceCount = 1;                          // meaningless unless shape == Poly
        std::optional<juce::Colour> colour;          // T152; unset falls back to the kind tint
    };

    MacroPortConfigDialog(juce::String macroName, std::vector<PortRow> ports);
    ~MacroPortConfigDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Fires once per user action; the caller runs it against the live macro+graph and then calls
    // refreshPorts() with a fresh snapshot — a port's IDENTITY (its node uuid) changes on add/
    // delete/shape-change, so rows are always rebuilt rather than patched in place.
    std::function<void(bool isInput, synth::MacroPortKind kind, MacroPortShape shape, int voiceCount,
                       const juce::String& name)>
        onAddPort;
    std::function<void(const juce::String& nodeUuid, const juce::String& newName)> onRenamePort;
    std::function<void(const juce::String& nodeUuid)> onDeletePort;
    /** `moveUp` true moves the port one step earlier in its own direction's draw order. The
     *  keyboard-accessible route to this (T153) is Cmd+Up/Cmd+Down on a row's colour swatch or
     *  Delete button (founder review round 4 — replaced the removed per-row Up/Down buttons). */
    std::function<void(const juce::String& nodeUuid, bool moveUp)> onReorderPort;
    /** T152 drag-to-reorder: fired once a drag ends on a new slot. `newIndexInGroup` is 0-based
     *  within the dragged row's OWN direction group (inputs vs outputs) — there is no way to
     *  express a cross-direction move through this signature, which is what keeps the "can't drag
     *  an input into the output section" constraint structural rather than a runtime check. */
    std::function<void(const juce::String& nodeUuid, int newIndexInGroup)> onReorderPortTo;
    std::function<void(const juce::String& nodeUuid, MacroPortShape newShape, int newVoiceCount)> onChangePortShape;
    /** T152 per-port colour: `newColour` is nullopt when the user resets to the kind-tint default
     *  (the swatch's right-click), otherwise the colour just picked. */
    std::function<void(const juce::String& nodeUuid, std::optional<juce::Colour> newColour)> onChangePortColour;
    std::function<void()> onRequestClose;

    /** Favourites shelf storage for the per-row colour picker (T152) — shares the same
     *  ApplicationProperties key every other ColourPickerPopup caller uses. Optional: nullptr
     *  (the default, and what every test gets) means in-memory-only favourites for this dialog's
     *  lifetime, exactly like ColourPickerPopup's own nullptr contract. */
    void setColourPickerPropertiesFile(juce::PropertiesFile* props) noexcept { colourPickerProps_ = props; }

    void refreshPorts(std::vector<PortRow> ports);

    bool keyPressed(const juce::KeyPress& key) override;

    // ---- Test seams: drive the REAL controls and read back the real row state, the same idiom
    // ExportAudioDialog's *ForTest methods use. ----
    int getRowCountForTest() const { return (int)rows_.size(); }
    juce::String getRowNodeUuidForTest(int row) const;
    juce::String getRowNameForTest(int row) const;
    bool getRowIsInputForTest(int row) const;

    void setNewPortNameForTest(const juce::String& name);
    void setNewPortDirectionForTest(bool isInput);
    void setNewPortKindForTest(synth::MacroPortKind kind);
    void setNewPortShapeForTest(MacroPortShape shape);
    void setNewPortVoiceCountForTest(int voices);
    void triggerAddPortForTest();

    void setRowNameForTest(int row, const juce::String& name);
    void commitRowNameForTest(int row); // simulates the editor losing focus / Return
    void triggerRowDeleteForTest(int row);
    // Selecting a new shape now commits immediately (the combo IS the "Apply Shape" gesture — see
    // the class comment), so this fires onChangePortShape itself, exactly like a real click would.
    void setRowShapeForTest(int row, MacroPortShape shape);
    // Only sets the voice-count field's text — matching how typing a number doesn't commit until
    // the field loses focus / Return, same as the name editor's own commit gesture below.
    void setRowVoiceCountForTest(int row, int voices);
    // Simulates the voices editor losing focus / Return: fires onChangePortShape with the row's
    // CURRENT shape selection and whatever the voices field currently holds.
    void commitRowVoiceCountForTest(int row);
    void triggerCloseForTest();

    // ---- T152 test seams: drag-to-reorder + per-port colour --------------------------------
    // Fires onReorderPortTo directly with the given target index — the same thing a real drag's
    // mouseUp does (MacroPortConfigDialog::endRowDrag calls this exact row method), without
    // needing to synthesize mouseDown/mouseDrag/mouseUp sequences to exercise the commit path.
    void dragRowToIndexInGroupForTest(int row, int newIndexInGroup);
    // The colour a row's swatch currently displays — the custom colour if one is set, otherwise
    // whatever kind tint it falls back to.
    juce::Colour getRowDisplayColourForTest(int row) const;
    bool getRowHasCustomColourForTest(int row) const;
    // Simulates picking a colour and closing the picker — fires onChangePortColour immediately,
    // matching the shape combo's "commits immediately" idiom rather than needing a real
    // juce::CallOutBox + juce::ColourSelector round trip in a headless test.
    void setRowColourForTest(int row, juce::Colour colour);
    // Simulates the swatch's right-click reset gesture — fires onChangePortColour(nodeUuid,
    // std::nullopt).
    void resetRowColourForTest(int row);
    // Builds the REAL juce::ColourSelector-backed popup a click on this row's swatch would show,
    // without going through a live juce::CallOutBox — exercises the exact onPreview/onCommit
    // lambdas (and their SafePointer guards) rather than the simplified setRowColourForTest
    // shortcut above. Pair with ColourPickerPopup::commitForTest(). Returns null for an
    // out-of-range row.
    std::unique_ptr<synth::ui::ColourPickerPopup> createRowColourPickerForTest(int row);

    // ---- T153 test seams: keyboard accessibility ------------------------------------------
    // Simulates Escape reaching the dialog (bubbled up from whatever child currently has focus,
    // or pressed with nothing focused) — see the keyPressed() override for why every TextEditor
    // ALSO needs its own onEscapeKey wired rather than relying on this bubble alone.
    void simulateEscapeKeyForTest() { keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)); }
    // Simulates a TextEditor's onEscapeKey firing directly — TextEditor::escapePressed() posts an
    // async command message in real use, which a headless test's message-less run loop never
    // pumps, so driving the wired lambda directly (the same idiom setComboSelectionForTest's
    // comment documents for onChange) is what actually exercises the close path.
    void simulateRowNameEscapeForTest(int row);
    void simulateNewPortNameEscapeForTest();
    // Simulates Return in the "Add a port" name field — the keyboard equivalent of clicking Add.
    void simulateNewPortNameReturnForTest();
    // Arrow-Up/Down navigation between rows' matching control (Colour swatch, Delete glyph
    // button — the non-text, non-combo controls; see moveRowFocus()'s own comment for why arrow
    // navigation is scoped to only these). computeArrowNavigationTargetRowForTest exercises the
    // bounds-computing logic directly rather than via real grabKeyboardFocus(), which requires an
    // on-screen peer this headless test has none of (Component::isShowing() gates it).
    enum class RowControl { Colour, Delete };
    int computeArrowNavigationTargetRowForTest(int fromRow, bool moveDown) const;
    // Exercises GlyphButton/PortColourSwatch's own keyPressed() override end to end: presses the
    // given key (Up/Down, or Cmd+Up/Cmd+Down when withCommandModifier is true) on the given row's
    // control and reports whether IT (not the dialog) consumed it. A bare arrow moves row focus
    // (moveRowFocus); Cmd+arrow fires onReorderPort instead (founder review round 4, replacing the
    // removed per-row Up/Down buttons) — the modifier is what disambiguates the two.
    bool simulateRowControlArrowKeyForTest(int row, RowControl control, bool moveDown,
                                           bool withCommandModifier = false);

    // ---- Founder review round 4 test seams: focus-visibility regression coverage -----------
    // Forces the given row's Delete button to paint its focus ring regardless of real keyboard
    // focus — grabKeyboardFocus() can't be exercised headlessly (see the comment above), so this
    // is the same "bypass the mouse/focus plumbing, drive the real paint path" idiom every other
    // *ForTest seam in this file already uses.
    void setRowFocusRingForcedForTest(int row, bool forced);
    // Snapshots just the given row's Delete button (createComponentSnapshot over its own local
    // bounds) — paired with setRowFocusRingForcedForTest to prove the focused/unfocused paint
    // output actually differs.
    juce::Image renderRowDeleteButtonForTest(int row) const;
    // The "Add a port" Add button's real laid-out bounds — a zero/near-zero height here is exactly
    // the founder-reported "button doesn't visibly appear" bug (a layout miscalculation, not a
    // colour bug; see the class comment's round 4 note).
    juce::Rectangle<int> getAddButtonBoundsForTest() const;

private:
    class PortRowComponent; // one row's controls + kind-tinted background; defined in the .cpp

    void rebuildRowComponents();
    static void populateShapeBox(juce::ComboBox& box);
    static MacroPortShape shapeFromComboIndex(int index);
    static int comboIndexFromShape(MacroPortShape shape);
    void updateNewPortVoicesVisibility();

    // ---- T152 drag-to-reorder plumbing (real mouse path; the *ForTest seams above bypass this
    // and call PortRowComponent::commitDragTo directly) ----
    void beginRowDrag(PortRowComponent& row);
    void updateRowDrag(PortRowComponent& row, juce::Point<int> screenPos);
    void endRowDrag(PortRowComponent& row);
    void clearDragIndicators();

    // Moves keyboard focus from `target` on `from` to the same control on the row immediately
    // above/below it in `rowControls_` — deliberately does NOT wrap past either end (an arrow key
    // running off the end of the list should do nothing, not jump to the opposite side) and
    // deliberately does NOT stop at the input/output boundary (this is plain focus navigation,
    // not a reorder — crossing sections to reach a port is exactly what a sighted user's eye
    // already does scanning down the column of rows).
    void moveRowFocus(PortRowComponent& from, RowControl target, bool moveDown);
    // The row index arrow-navigation from `fromRow` would land on, or -1 at either end of the
    // list (no wraparound) — the one place this bounds math lives, shared by moveRowFocus() and
    // computeArrowNavigationTargetRowForTest() so they can never disagree.
    int arrowNavigationTargetRow(int fromRow, bool moveDown) const;

    // Lays out (apply=true) or just measures (apply=false, no component touched) the Inputs/
    // Outputs sections at the given content width, returning the total height either way — ONE
    // function so the measurement used to size the dialog can never drift from the layout that
    // actually runs, which two separate "compute height" / "lay out" functions risked.
    int layOutOrMeasureRows(bool apply, int width);
    int idealDialogHeight();

    juce::String macroName_;
    std::vector<PortRow> rows_;
    juce::OwnedArray<PortRowComponent> rowControls_;

    juce::Label titleLabel_;
    juce::Label newPortSectionLabel_{"newPortSectionLabel", "Add a port"};
    juce::TextEditor newNameEditor_;
    juce::ComboBox newDirectionBox_;
    juce::ComboBox newKindBox_;
    juce::ComboBox newShapeBox_;
    juce::Label newVoicesLabel_{"newVoicesLabel", "Voices"};
    juce::TextEditor newVoicesEditor_;
    juce::TextButton addButton_{"Add"};
    juce::TextButton closeButton_{"Close"};
    juce::Rectangle<int> addBlockBounds_; // for paint()'s grouping panel behind the block above

    juce::Viewport rowsViewport_;
    juce::Component rowsContent_;
    juce::Label inputsHeader_{"inputsHeader", "INPUTS"};
    juce::Label outputsHeader_{"outputsHeader", "OUTPUTS"};
    juce::Label inputsEmptyHint_{"inputsEmptyHint", "No inputs yet"};
    juce::Label outputsEmptyHint_{"outputsEmptyHint", "No outputs yet"};

    static constexpr int kRowHeight = 32;
    static constexpr int kRowGap = 4;
    static constexpr int kSectionHeaderHeight = 18;
    static constexpr int kSectionGap = 10;
    static constexpr int kEmptyHintHeight = 18;
    static constexpr int kAddRowHeight = 26;
    // The "Add a port" panel's total content height: section label + newRow1 + a 6px gap +
    // newRow2, plus 8px of top/bottom inner padding each — computed ONCE so resized() (which lays
    // out) and idealDialogHeight() (which measures) can never drift apart the way they did before
    // round 4 (the panel was budgeted for only 2 of its 3 rows, so newRow2 — the name field and
    // Add button — got squeezed to zero height and effectively vanished).
    static constexpr int kAddBlockHeight = 8 + kAddRowHeight + kAddRowHeight + 6 + kAddRowHeight + 8;
    static constexpr int kMargin = 14;
    static constexpr int kDialogWidth = 580;
    static constexpr int kMinDialogHeight = 300;
    static constexpr int kMaxDialogHeight = 620;

    juce::PropertiesFile* colourPickerProps_ = nullptr; // T152; see setColourPickerPropertiesFile

    // T152 drag state — empty/-1 whenever no drag is in progress. Only ever set from within a
    // single beginRowDrag/updateRowDrag/endRowDrag sequence (one at a time: JUCE delivers mouse
    // events to at most one dragged component).
    juce::String draggingNodeUuid_;
    int dragDropIndexInGroup_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroPortConfigDialog)
};

/**
 * @brief "Create ports for the crossing cables?" modal (founder-review fix F5, docs/macros.md §7
 * item 6.2) — shown by GraphEditor::requestGroupSelectionIntoMacro() the first time a group has a
 * cable crossing its would-be boundary and the auto-port preference is still Unset.
 *
 * Pure UI, the same split as MacroPortConfigDialog above: no GraphEditor/MacroSet reference of its
 * own, one intent callback fired on a button press. Deliberately NOT reused as a nested class of
 * MacroPortConfigDialog — the two dialogs share nothing but a translation unit, and keeping this
 * one in the same file pair, rather than a new Source/UI cpp/h pair, avoids a five-CMakeLists edit
 * for a component this small.
 *
 * T153 (founder review round 3): Escape used to be a silent no-op here — GraphEditor::
 * showMacroAutoPortModal launches this in a juce::DialogWindow whose default
 * `escapeKeyTriggersCloseButton` just hides the window (Component::setVisible(false)) without
 * ever calling `onChoice`, so `respond` never ran: no macro got created, and no status message
 * explained why. DECISION: Escape now behaves exactly like "Leave Cables As Is" — the least
 * surprising reading of "close/cancel without creating a port, same as clicking away," since the
 * user already asked to group these modules (Cmd+G or the menu item got them here); this modal is
 * only deciding a secondary refinement (whether to also auto-create boundary ports), and Escape
 * aborting the WHOLE grouping would be the surprising outcome, not this one. `remember` is always
 * forced to false on the Escape path regardless of the toggle's current state (it defaults ON),
 * so an reflexive Escape press can never silently pin "always leave cables as-is" as a permanent
 * preference the way a deliberate button click legitimately can.
 */
class MacroAutoPortPromptDialog : public juce::Component {
public:
    /** `crossingPortCount` is the number of macro ports Create Ports would splice in (i.e.
     *  buildMacroPortCrossingPlan().size() — one per distinct internal jack/direction, already
     *  deduped, NOT a raw cable count: two cables into the same jack share one port and count as
     *  one here). Only changes the message text; plays no role in the choice itself. */
    explicit MacroAutoPortPromptDialog(int crossingPortCount);
    ~MacroAutoPortPromptDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    bool keyPressed(const juce::KeyPress& key) override;

    /** Fires exactly once, on either button (or Escape — see the class comment's T153 decision).
     *  `createPorts` true = "Create Ports", false = "Leave Cables As Is"; `remember` mirrors the
     *  "Remember my choice" toggle's state at the moment of the click (always false on the Escape
     *  path). The caller (GraphEditor) closes the DialogWindow from this callback — this
     *  component never closes its own host window. */
    std::function<void(bool createPorts, bool remember)> onChoice;

    // ---- Test seams: drive the real controls, the same idiom MacroPortConfigDialog's use. ----
    void setRememberChoiceForTest(bool remember);
    bool getRememberChoiceForTest() const;
    void triggerCreatePortsForTest();
    void triggerLeaveCablesAsIsForTest();
    void simulateEscapeKeyForTest() { keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)); }

private:
    juce::Label titleLabel_;
    juce::Label messageLabel_;
    juce::ToggleButton rememberToggle_{"Remember my choice"};
    juce::TextButton createPortsButton_{"Create Ports"};
    juce::TextButton leaveAsIsButton_{"Leave Cables As Is"};

    static constexpr int kMargin = 16;
    static constexpr int kWidth = 420;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroAutoPortPromptDialog)
};

} // namespace synth::ui
