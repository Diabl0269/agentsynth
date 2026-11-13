#pragma once

#include "UI/Graph/CardLayoutEditor/CardLayoutEditorModel.h"
#include "UI/Graph/CardLayoutEditor/CardLayoutEditorSource.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReorderDrag/ReorderDragSession.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class ShortcutManager;

namespace synth::ui {

class CardLayoutEditorRow;

/**
 * The "Edit Layout..." panel (docs/layout/module-card-layout.md#editing-a-layout): every control the
 * card can show, grouped by section, with a tick (shown or hidden), drag or Cmd+Up/Down to reorder, a
 * click or Enter on a name to rename it, a widget choice per control, + Add group, a search, Apply to
 * (this module / every module of its kind), presets and reset. Opened in a juce::CallOutBox beside
 * the card. There is no OK button: every edit is written through the source at once and the card
 * re-lays out live. What is stored and how it undoes is the source's (CardLayoutEditorSource).
 */
class CardLayoutEditorComponent : public juce::Component {
public:
    /** `shortcuts` may be null (the default keys apply); if set it must outlive this panel. */
    explicit CardLayoutEditorComponent(std::unique_ptr<CardLayoutEditorSource> source,
                                       const ShortcutManager* shortcuts = nullptr);
    ~CardLayoutEditorComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 440;
    static constexpr int kHeight = 480;

    CardLayoutEditorSource& getSource() noexcept { return *source_; }

    // ---- Test seams: drive the real controls / read back the real state ----
    int getVisibleRowCountForTest() const;
    juce::String getVisibleRowParamIdForTest(int row) const;
    bool getVisibleRowCheckedForTest(int row) const;
    juce::String getVisibleRowLabelForTest(int row) const;
    void setSearchTextForTest(const juce::String& text);
    void triggerRowToggleForTest(int row);
    void setRowLabelForTest(int row, const juce::String& text);
    void commitRowLabelForTest(int row);
    /** `newIndexAmongChecked` is 0-based within the first group's placed controls. */
    void dragCheckedRowToIndexForTest(const juce::String& paramId, int newIndexAmongChecked);
    juce::Component* getRowDragHandleForTest(int row);
    juce::Component& getRowsContentForTest() noexcept { return rowsContent_; }
    juce::Rectangle<int> getRowBoundsForTest(int row) const;
    bool isRowDragActiveForTest() const noexcept { return rowDrag_.isReordering(); }
    bool sendEscapeToRowDragForTest() { return rowDrag_.sendEscapeForTest(); }
    bool isApplyToAllInstancesForTest() const noexcept { return applyToAll_; }
    void setApplyToAllInstancesForTest(bool allInstances);
    juce::StringArray getPresetNamesForTest() const;
    void selectPresetForTest(const juce::String& name);
    void triggerSaveAsPresetForTest(const juce::String& name);
    void triggerDeletePresetForTest(const juce::String& name);
    void triggerResetToAutomaticForTest();
    int getMissingParameterCountForTest() const { return model_.missingNames().size(); }
    juce::String getMissingParameterLineForTest() const { return missingLabel_.getText(); }
    bool isMissingLineVisibleForTest() const { return missingLabel_.isVisible(); }
    int getRowsTopForTest() const { return rowsViewport_.getY(); }
    /** The visible row index whose key is `key` (a parameter id, or "#<n>" for group n), or -1. */
    int findRowForTest(const juce::String& key) const;
    CardLayoutEditorRow* getRowForTest(int row) const;
    /** Delivers `key` to row `row` as a real key press on the focused row would arrive. */
    bool pressKeyOnRowForTest(int row, const juce::KeyPress& key);
    void selectWidgetForTest(int row, CardWidget widget);
    void triggerAddGroupForTest();
    /** The key of the row the panel last moved keyboard focus to. */
    const juce::String& getFocusedRowKeyForTest() const noexcept { return focusedKey_; }
    juce::Button& getAddGroupButtonForTest() noexcept { return addGroupButton_; }

private:
    // ---- Lifecycle / layout (CardLayoutEditorComponent.cpp) ----
    void buildChrome();
    void buildScopeAndPresetControls();
    void layOutRows();

    // ---- Row list + edits (CardLayoutEditorComponentRows.cpp) ----
    void rebuildRows();
    void addRow(const CardLayoutEditorModel::Row& row);
    void wireRow(CardLayoutEditorRow& row, const CardLayoutEditorModel::Row& model);
    void setChecked(const juce::String& paramId, bool checked);
    void renameRow(const CardLayoutEditorModel::Row& row, const juce::String& text);
    void addGroup();
    void commitAndRebuild(const juce::String& focusKey = {});
    void updateMissingLabel();
    void applyCurrentLayout();

    // ---- Keyboard (CardLayoutEditorComponentKeyboard.cpp) ----
    bool handleRowKey(CardLayoutEditorRow& row, const juce::KeyPress& key);
    bool matchesAction(const juce::KeyPress& key, const char* actionId, const juce::KeyPress& fallback) const;
    void focusRow(const juce::String& key);
    void refreshShortcutTooltips();

    // ---- Drag-reorder (CardLayoutEditorComponentDrag.cpp) ----
    void beginRowDrag(const juce::String& key, const juce::MouseEvent& e);
    void updateRowDrag(const juce::MouseEvent& e);
    void endRowDrag();
    /** Places the dragged group's rows from the animator while a drag is live or settling, and every
     *  row at its static slot otherwise. */
    void placeDragRows();

    // ---- Scope + presets (CardLayoutEditorComponentScope.cpp) ----
    void refreshPresetCombo();
    void loadPreset(const juce::String& name);
    void commitSaveAsPreset(const juce::String& name);
    void commitDeletePreset(const juce::String& name);
    void resetToDefault();
    void promptSaveAsPreset();
    void promptDeletePreset();

    std::unique_ptr<CardLayoutEditorSource> source_;
    const ShortcutManager* shortcuts_;
    std::vector<CardLayoutEditorParam> params_; // captured once, at construction
    CardLayoutEditorModel model_;
    bool applyToAll_ = false;
    juce::String focusedKey_;

    juce::Label titleLabel_;
    juce::Label applyToLabel_{"applyToLabel", "Apply to:"};
    juce::ComboBox applyToCombo_;
    juce::Label presetLabel_{"presetLabel", "Preset:"};
    juce::ComboBox presetCombo_;
    juce::TextButton saveAsButton_{"Save as..."};
    juce::TextButton deleteButton_{"Delete"};
    juce::TextButton resetButton_;
    juce::TextEditor searchEditor_;
    juce::TextButton addGroupButton_{"+ Add group"};
    juce::Label missingLabel_;
    synth::ui::FadeVisibility missingFade_{
        &missingLabel_}; ///< The "parameters missing" line fades and its height follows.

    juce::Viewport rowsViewport_;
    juce::Component rowsContent_;
    juce::OwnedArray<CardLayoutEditorRow> rows_;

    // Drag state, empty whenever no drag is in progress. The animator's keys are positions in
    // dragKeys_ (the dragged group's visible rows as they stood at the press); rows are matched back by
    // key because the commit rebuilds every row. dragSlotStarts_[p] is the y of slot p.
    ReorderDragSession rowDrag_{*this, [this] { placeDragRows(); }};
    std::vector<juce::String> dragKeys_;
    std::vector<float> dragSlotStarts_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardLayoutEditorComponent)
};

} // namespace synth::ui
