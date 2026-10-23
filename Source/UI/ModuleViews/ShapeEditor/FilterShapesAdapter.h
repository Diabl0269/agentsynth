#pragma once

// FilterShapesAdapter.h -- the Filter's Custom shapes as a ShapeListAdapter: reads and writes the module's shape
// list; the undo bracket and the morph cursor come from ModuleShapesAdapter. The card's map and the editor window
// share one.
#include "ModuleShapesAdapter.h"
#include "Modules/FilterModule.h"
#include "UI/ModuleViews/FilterSpectrumAnalyser.h"
#include <functional>

namespace synth::ui {

class FilterShapesAdapter : public ModuleShapesAdapter {
public:
    /** What the adapter cannot do itself (it knows no GraphEditor): the card supplies them. */
    struct PitchHooks {
        /** The Filter's Pitch input state. Unset: Connected. */
        std::function<PitchInput()> state;
        /** Cables the voice chain's Pitch source to the Filter's Pitch input; called inside the undo step. */
        std::function<void()> connect;
    };

    /** `undo` may be null (no undo steps). The module and graph must outlive the adapter. */
    FilterShapesAdapter(FilterModule& module, AppUndoManager* undo, juce::AudioProcessorGraph& graph,
                        juce::AudioProcessorGraph::NodeID nodeId, juce::String moduleName);

    std::vector<ShapeItem> getShapes() const override;
    void setShapes(const std::vector<ShapeItem>& shapes) override;
    int getGeneration() const override;
    int getMaxShapes() const override;
    ShapeItem makeShapeAt(float x, float y) const override;

    std::optional<CurveEditorComponent::AxisGuides> getAxisGuides() const override;
    std::optional<ShapeGridOffer> getGridOffer() const override;
    juce::String getPreferenceKind() const override { return "filter"; }
    bool hasSpectrum() const override { return true; }
    const std::vector<float>* updateSpectrum() override;
    std::vector<ShapeStartFrom> getStartFrom() const override;
    ShapeDrawRange getDrawRange() const override;
    juce::String getWindowTitle() const override;
    bool hasPerNote() const override { return true; }
    CustomFilterPerNote getPerNote() const override;
    void setPerNote(const CustomFilterPerNote& perNote) override;
    PitchInput pitchInput() const override;
    bool setPerNoteConnectingPitch(const CustomFilterPerNote& perNote) override;
    float getReferenceLevel() const override;

    void setPitchHooks(PitchHooks hooks) { hooks_ = std::move(hooks); }

private:
    FilterModule& filter_;
    AppUndoManager* undo_;
    juce::AudioProcessorGraph& graph_;
    PitchHooks hooks_;
    FilterSpectrumAnalyser spectrum_;
};

} // namespace synth::ui
