#pragma once

// LfoShapesAdapter.h -- the LFO's Custom shapes as a ShapeListAdapter: reads and writes the module's shape list; the
// undo bracket and the morph cursor come from ModuleShapesAdapter. The same editor window the Filter's shapes use
// edits them.
#include "ModuleShapesAdapter.h"
#include "Modules/LFOModule.h"

namespace synth::ui {

class LfoShapesAdapter : public ModuleShapesAdapter {
public:
    /** `undo` may be null (no undo steps). The module and graph must outlive the adapter. */
    LfoShapesAdapter(LFOModule& module, AppUndoManager* undo, juce::AudioProcessorGraph& graph,
                     juce::AudioProcessorGraph::NodeID nodeId, juce::String moduleName);

    std::vector<ShapeItem> getShapes() const override;
    void setShapes(const std::vector<ShapeItem>& shapes) override;
    int getGeneration() const override;
    int getMaxShapes() const override;
    ShapeItem makeShapeAt(float x, float y) const override;

    std::vector<ShapeStartFrom> getStartFrom() const override;
    juce::String getWindowTitle() const override;

private:
    LFOModule& lfo_;
};

} // namespace synth::ui
