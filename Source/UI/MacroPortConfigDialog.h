#pragma once

#include "../MacroSet.h"
#include "../Modules/MacroPortShape.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
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
    /** `moveUp` true moves the port one step earlier in its own direction's draw order. */
    std::function<void(const juce::String& nodeUuid, bool moveUp)> onReorderPort;
    std::function<void(const juce::String& nodeUuid, MacroPortShape newShape, int newVoiceCount)> onChangePortShape;
    std::function<void()> onRequestClose;

    void refreshPorts(std::vector<PortRow> ports);

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
    void triggerRowMoveUpForTest(int row);
    void triggerRowMoveDownForTest(int row);
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

private:
    class PortRowComponent; // one row's controls + kind-tinted background; defined in the .cpp

    void rebuildRowComponents();
    static void populateShapeBox(juce::ComboBox& box);
    static MacroPortShape shapeFromComboIndex(int index);
    static int comboIndexFromShape(MacroPortShape shape);
    void updateNewPortVoicesVisibility();

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
    static constexpr int kMargin = 14;
    static constexpr int kDialogWidth = 580;
    static constexpr int kMinDialogHeight = 300;
    static constexpr int kMaxDialogHeight = 620;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroPortConfigDialog)
};

} // namespace synth::ui
