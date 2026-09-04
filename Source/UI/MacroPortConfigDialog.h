#pragma once

#include "../MacroSet.h"
#include "../Modules/MacroPortShape.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {

/**
 * @brief The "Configure I/O" modal for one Macro (P8-15b, T140).
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
 * and reads back what each button would send, with no AudioEngine/GraphEditor involved at all.
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
    void setRowShapeForTest(int row, MacroPortShape shape);
    void setRowVoiceCountForTest(int row, int voices);
    void triggerRowApplyShapeForTest(int row);
    void triggerCloseForTest();

private:
    struct RowControls {
        juce::Label directionLabel{"directionLabel", juce::String()};
        juce::TextEditor nameEditor;
        juce::Label kindLabel{"kindLabel", juce::String()};
        juce::ComboBox shapeBox;       // AudioCV rows only; disabled/hidden for MIDI
        juce::TextEditor voicesEditor; // enabled only when shapeBox reads Poly-N
        juce::TextButton applyShapeButton{"Apply Shape"};
        juce::TextButton upButton{"Up"};
        juce::TextButton downButton{"Down"};
        juce::TextButton deleteButton{"Delete"};
    };

    void rebuildRowComponents();
    void updateVoicesEnablement(RowControls& rc);
    static void populateShapeBox(juce::ComboBox& box);
    static MacroPortShape shapeFromComboIndex(int index);
    static int comboIndexFromShape(MacroPortShape shape);
    int contentHeightForCurrentRows() const;

    juce::String macroName_;
    std::vector<PortRow> rows_;
    juce::OwnedArray<RowControls> rowControls_;

    juce::Label titleLabel_;
    juce::Label newPortSectionLabel_{"newPortSectionLabel", "Add a port"};
    juce::TextEditor newNameEditor_;
    juce::ComboBox newDirectionBox_;
    juce::ComboBox newKindBox_;
    juce::ComboBox newShapeBox_;
    juce::TextEditor newVoicesEditor_;
    juce::TextButton addButton_{"Add"};
    juce::TextButton closeButton_{"Close"};

    static constexpr int kRowHeight = 32;
    static constexpr int kNewPortRowHeight = 34;
    static constexpr int kMargin = 12;
    static constexpr int kDialogWidth = 800;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroPortConfigDialog)
};

} // namespace synth::ui
