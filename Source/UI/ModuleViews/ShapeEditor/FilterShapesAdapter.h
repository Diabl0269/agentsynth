#pragma once

// FilterShapesAdapter.h -- the Filter's Custom shapes as a ShapeListAdapter: reads and writes the module's shape
// list, owns the morph cursor's parameters and records undo steps. The card's map and the editor window share one.
#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "ShapeListAdapter.h"

namespace synth::ui {

class FilterShapesAdapter : public ShapeListAdapter {
public:
    /** `undo` may be null (no undo steps). The module and graph must outlive the adapter. */
    FilterShapesAdapter(FilterModule& module, AppUndoManager* undo, juce::AudioProcessorGraph& graph,
                        juce::AudioProcessorGraph::NodeID nodeId, juce::String moduleName);

    std::vector<ShapeItem> getShapes() const override;
    void setShapes(const std::vector<ShapeItem>& shapes) override;
    int getGeneration() const override;
    void beginEdit() override;
    void endEdit() override;
    int getMaxShapes() const override;
    ShapeItem makeShapeAt(float x, float y) const override;

    bool hasCursor() const override { return true; }
    juce::Point<float> getCursor() const override;
    void setCursor(juce::Point<float> position) override;
    void beginCursorGesture() override;
    void endCursorGesture() override;

    std::optional<CurveEditorComponent::AxisGuides> getAxisGuides() const override;
    std::vector<ShapeStartFrom> getStartFrom() const override;
    ShapeDrawRange getDrawRange() const override;
    juce::String getWindowTitle() const override;
    float getReferenceLevel() const override;
    /** The name the window title carries; the card refreshes it before opening. */
    void setModuleName(juce::String name) { moduleName_ = std::move(name); }

private:
    FilterModule& module_;
    AppUndoManager* undo_;
    juce::AudioProcessorGraph& graph_;
    juce::AudioProcessorGraph::NodeID nodeId_;
    juce::String moduleName_;
    juce::var before_;
    bool editOpen_ = false;
    bool cursorGestureOpen_ = false;
};

} // namespace synth::ui
