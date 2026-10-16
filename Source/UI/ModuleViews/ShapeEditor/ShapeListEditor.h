#pragma once

#include "ShapeListAdapter.h"
#include "ShapeMapController.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {

/** The content of the shape editor window: a large morph map on the left; on the right a row of shape chips, the
 *  curve editor for the selected shape and the buttons that act on it (Add shape, Duplicate, Delete, Rename,
 *  Start from, Draw); Done in the bottom bar, which also leaves a slot for per-note settings.
 *
 *  It edits whatever the `ShapeListAdapter` holds, so it is not tied to the Filter. Edits are heard live (every
 *  change goes to the adapter at once) and each gesture or button is one undo step.
 *
 *  Keyboard: Tab visits the map and its dots and cursor, the chips, the curve editor, the buttons and Done, in that
 *  order. Escape closes the window (`onDone`, else the hosting window).
 */
class ShapeListEditor
    : public juce::Component
    , private juce::Timer {
public:
    static constexpr int kWidth = 880;
    static constexpr int kHeight = 560;

    /** `selected` is clamped into the list. */
    ShapeListEditor(std::shared_ptr<ShapeListAdapter> adapter, int selected);
    ~ShapeListEditor() override;

    void setSelected(int index);
    int getSelected() const noexcept { return selected_; }
    int getNumShapes() const noexcept { return (int)shapes_.size(); }
    /** Re-reads the adapter (the polling timer does this when its generation moves). */
    void refresh();

    /** Closing: Done and Escape. Unset closes the hosting window. */
    std::function<void()> onDone;
    /** Shows a menu; unset opens it as a real popup. A test replaces it to inspect the menu. */
    std::function<void(juce::PopupMenu&)> showMenu;

    /** Stamps the curve of start-from entry `index` onto the selected shape. */
    void applyStartFrom(int index);
    /** Applies a draw shape to the selected shape's curve. Free only returns the editor to point editing. */
    void applyDrawShape(DrawShape shape);

    // ---------- inspection ----------
    MorphMap& getMap() noexcept { return map_; }
    CurveEditorComponent& getCurveEditor() noexcept { return curve_; }
    ShapeMapController& getController() noexcept { return controller_; }
    juce::TextButton& getAddButton() noexcept { return addButton_; }
    juce::TextButton& getDuplicateButton() noexcept { return duplicateButton_; }
    juce::TextButton& getDeleteButton() noexcept { return deleteButton_; }
    juce::TextButton& getRenameButton() noexcept { return renameButton_; }
    juce::TextButton& getStartFromButton() noexcept { return startFromButton_; }
    juce::TextButton& getDrawButton() noexcept { return drawButton_; }
    juce::TextButton& getDoneButton() noexcept { return doneButton_; }
    juce::Component* getChip(int index) const;
    /** An empty area in the bottom bar reserved for per-note settings. */
    juce::Component& getBottomBarSlot() noexcept { return bottomSlot_; }
    ShapeDrawRange getDrawRange() const;
    juce::String getWindowTitle() const { return adapter_->getWindowTitle(); }

    void resized() override;
    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    class Chip;
    class ChipStrip;

    void timerCallback() override;
    void loadSelectedCurve();
    void writeCurve();
    void edit(const std::function<void(std::vector<ShapeItem>&)>& mutate);
    void openStartFromMenu();
    void openDrawFlyout();
    void showMenuNow(juce::PopupMenu& menu, juce::Component& target);
    void syncAll();
    void updateButtonStates();

    std::shared_ptr<ShapeListAdapter> adapter_;
    std::vector<ShapeItem> shapes_;
    int selected_ = 0;
    int lastGeneration_ = -1;
    bool curveGestureOpen_ = false;

    MorphMap map_;
    ShapeMapController controller_;
    std::unique_ptr<ChipStrip> chips_;
    CurveEditorComponent curve_;
    juce::TextButton addButton_{"Add shape"};
    juce::TextButton duplicateButton_{"Duplicate"};
    juce::TextButton deleteButton_{"Delete"};
    juce::TextButton renameButton_{"Rename"};
    juce::TextButton startFromButton_{"Start from"};
    juce::TextButton drawButton_{"Draw"};
    juce::TextButton doneButton_{"Done"};
    juce::Component bottomSlot_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShapeListEditor)
};

} // namespace synth::ui
