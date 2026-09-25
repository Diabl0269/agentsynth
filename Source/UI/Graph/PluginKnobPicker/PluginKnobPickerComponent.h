#pragma once

#include "AppUndoManager.h"
#include "Modules/CardLayout.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {

class PluginKnobPickerRow;
class PluginKnobPickerTouchCapture;

/**
 * "Choose knobs..." popover (FRO132, docs/control/plugin-card-layout.md#choosing-knobs) -- search,
 * tick/untick, drag-reorder and per-slot label for a hosted plugin's parameters, an Apply-to scope
 * (this instance / every instance of the plugin), named presets, and touch-to-add. Opened as a
 * `juce::CallOutBox` anchored to the card (`ModuleComponent::showPluginKnobPicker`) or the card's
 * right-click menu; there is no OK button -- every gesture applies immediately to whichever scope
 * "Apply to" currently names, exactly like the card's own knobs do.
 *
 * Reached only through a `juce::WeakReference<HostedPluginModule>`: the module can be destroyed out
 * from under an open popover (the card's own "Replace with...", or a node delete) exactly as
 * `ModuleComponent::HostedCardBinding` already has to handle. Every mutating method checks
 * `module_.get()` first and is a silent no-op once it is gone -- the popover simply stops doing
 * anything further; `juce::CallOutBox` dismissal (Esc, click-away) needs no cooperation from this
 * class.
 *
 * `allParams_` (the instance's parameter list) is captured ONCE at construction, matching the fact
 * that the card row this opens from is itself only shown for a plugin that already has a live
 * instance; an instance swap while the popover is open (rare -- Replace/reload while the picker is
 * up) is not specially handled, the same known v1 limitation `PluginKnobPickerTouchCapture` already
 * documents for its own listener registration.
 *
 * "N parameters missing" (the founder decision
 * docs/control/plugin-card-layout.md#the-cardlayout-type-and-where-a-layout-comes-from names): a slot loaded from the
 * resolved layout / a stored preset whose `paramId` is not among `allParams_` is split into `missingSlots_` rather than
 * shown as a row -- there is no checkbox for a parameter that doesn't exist. The very next apply (any tick, reorder,
 * label, scope switch, or preset load) writes `workingSlots_` only, so the missing ones are silently dropped from
 * storage at that point, matching "dropped on next save".
 */
class PluginKnobPickerComponent final : public juce::Component {
public:
    /** `graph`/`nodeId` identify the node for `undoManager->recordNodeExtraStateChange`; `undoManager`
     *  and `store` may be null (an unwired test rig, or a plugin with no card-layout store injected --
     *  see GraphEditor::getPluginCardLayoutStore()'s own null contract). `module` must have a live
     *  instance; the button/menu item that constructs this only exist once a card is already showing
     *  the plugin's parameters. */
    PluginKnobPickerComponent(HostedPluginModule& module, PluginCardLayoutStore* store,
                              juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                              AppUndoManager* undoManager);
    ~PluginKnobPickerComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Fired once per touch-to-add arming, forwarded from PluginKnobPickerTouchCapture -- the caller
     *  (ModuleComponent::showPluginKnobPicker) wires this to the same
     *  GraphEditor::onOpenPluginEditorRequested the card's own "Open Editor" button uses. */
    std::function<void()> onOpenPluginEditorRequested;

    static constexpr int kWidth = 420;
    static constexpr int kHeight = 460;

    // ---- Test seams: drive the real controls / read back the real state, the MacroPortConfigDialog
    // idiom (dragCheckedRowToIndexForTest mirrors dragRowToIndexInGroupForTest exactly: it calls the
    // same commit function a real drag's mouseUp calls, without synthesizing mouse events). ----
    int getVisibleRowCountForTest() const { return rows_.size(); }
    juce::String getVisibleRowParamIdForTest(int row) const;
    bool getVisibleRowCheckedForTest(int row) const;
    juce::String getVisibleRowLabelForTest(int row) const;
    void setSearchTextForTest(const juce::String& text);
    void triggerRowToggleForTest(int row);
    void setRowLabelForTest(int row, const juce::String& text);
    void commitRowLabelForTest(int row);
    /** `newIndexAmongChecked` is 0-based within the checked group only, exactly like
     *  MacroPortConfigDialog::dragRowToIndexInGroupForTest scopes to one direction group. */
    void dragCheckedRowToIndexForTest(const juce::String& paramId, int newIndexAmongChecked);

    bool isApplyToAllInstancesForTest() const noexcept { return applyToAllInstances_; }
    void setApplyToAllInstancesForTest(bool allInstances);

    juce::StringArray getPresetNamesForTest() const;
    void selectPresetForTest(const juce::String& name);
    void triggerSaveAsPresetForTest(const juce::String& name);
    void triggerDeletePresetForTest(const juce::String& name);
    void triggerResetToAutomaticForTest();

    void setTouchToAddArmedForTest(bool armed);
    bool isTouchToAddArmedForTest() const;
    /** Delivers a gesture-start for live parameter `parameterIndex` through the real
     *  PluginKnobPickerTouchCapture -- see its own simulateGestureStartForTest for what "real" means
     *  here (the actual queue+AsyncUpdater hop, not a direct model mutation). */
    void simulateTouchGestureForTest(int parameterIndex);

    int getMissingParameterCountForTest() const { return static_cast<int>(missingSlots_.size()); }
    juce::String getMissingParameterLineForTest() const { return missingLabel_.getText(); }

    juce::AudioProcessorGraph::NodeID getNodeIdForTest() const noexcept { return nodeId_; }

private:
    struct ParamInfo {
        juce::String paramId;
        int index = -1;
        juce::String displayName;
    };

    // ---- Lifecycle / layout (PluginKnobPickerComponent.cpp) ----
    void buildChrome();
    void layOutRows();

    // ---- Row list + apply (PluginKnobPickerComponentRows.cpp) ----
    void rebuildRows();
    void addRow(const ParamInfo& info, bool checked, const juce::String& labelOverride);
    void setParamChecked(const juce::String& paramId, bool checked);
    void setParamLabel(const juce::String& paramId, const juce::String& text);
    void commitReorder(const juce::String& paramId, int newIndexAmongChecked);
    void handleParameterTouched(int parameterIndex);
    /** Splits `slots` (raw, as stored) into workingSlots_ (resolves against allParams_) and
     *  missingSlots_ (does not) -- see the class comment's "N parameters missing" paragraph. */
    void partitionSlots(std::vector<CardSlot> slots);
    void updateMissingLabel();
    void applyCurrentLayout();

    // ---- Scope + presets (PluginKnobPickerComponentScope.cpp) ----
    void refreshPresetCombo();
    void loadPreset(const juce::String& name);
    void commitSaveAsPreset(const juce::String& name);
    void commitDeletePreset(const juce::String& name);
    void resetToAutomatic();
    void promptSaveAsPreset();
    void promptDeletePreset();

    juce::WeakReference<HostedPluginModule> module_;
    PluginCardLayoutStore* store_; // not owned; may be null; must outlive this popover
    juce::AudioProcessorGraph& graph_;
    juce::AudioProcessorGraph::NodeID nodeId_;
    AppUndoManager* undoManager_; // not owned; may be null

    PluginIdentity identity_;
    std::vector<ParamInfo> allParams_;   // snapshot at construction; see the class comment
    std::vector<CardSlot> workingSlots_; // checked, in order; what the next apply writes
    std::vector<CardSlot> missingSlots_; // loaded but unresolvable against allParams_
    bool applyToAllInstances_ = false;

    juce::Label titleLabel_;
    juce::Label applyToLabel_{"applyToLabel", "Apply to:"};
    juce::ComboBox applyToCombo_;
    juce::Label presetLabel_{"presetLabel", "Preset:"};
    juce::ComboBox presetCombo_;
    juce::TextButton saveAsButton_{"Save as..."};
    juce::TextButton deleteButton_{"Delete"};
    juce::TextButton resetButton_{"Reset to automatic"};
    juce::TextEditor searchEditor_;
    juce::ToggleButton touchToAddToggle_{"Touch in the plugin editor to add"};
    juce::Label missingLabel_;

    juce::Viewport rowsViewport_;
    juce::Component rowsContent_;
    juce::OwnedArray<PluginKnobPickerRow> rows_;

    std::unique_ptr<PluginKnobPickerTouchCapture> touchCapture_;

    // Drag-reorder state -- one drag at a time (JUCE delivers mouse events to at most one dragged
    // row), reset once endRowDrag runs. Mirrors MacroPortConfigDialog's draggingNodeUuid_ pattern.
    juce::String draggingParamId_;
    int dragStartIndexAmongChecked_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginKnobPickerComponent)
};

} // namespace synth::ui
