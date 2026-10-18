// FilterShapesAdapter.cpp -- the Filter's Custom shapes behind the generic shape list interface.
#include "FilterShapesAdapter.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/Filter/CustomFilterBlend.h"

namespace synth::ui {

namespace {
using synth::CustomFilterShape;
using synth::CustomFilterShapes;

ShapeItem toItem(const CustomFilterShape& shape) { return {shape.name, shape.curve, shape.position}; }
} // namespace

FilterShapesAdapter::FilterShapesAdapter(FilterModule& module, AppUndoManager* undo, juce::AudioProcessorGraph& graph,
                                         juce::AudioProcessorGraph::NodeID nodeId, juce::String moduleName)
    : ModuleShapesAdapter(module, undo, graph, nodeId, std::move(moduleName))
    , filter_(module)
    , undo_(undo)
    , graph_(graph) {}

std::vector<ShapeItem> FilterShapesAdapter::getShapes() const {
    std::vector<ShapeItem> out;
    for (const auto& shape : filter_.getCustomShapes().shapes)
        out.push_back(toItem(shape));
    return out;
}

/** The module sanitises what it is given (names, positions, curves, the shape cap) and hands the audio thread a
    new table. */
void FilterShapesAdapter::setShapes(const std::vector<ShapeItem>& shapes) {
    CustomFilterShapes list;
    for (const auto& item : shapes)
        list.shapes.push_back({item.name, item.curve, item.position});
    filter_.setCustomShapes(list);
}

int FilterShapesAdapter::getGeneration() const { return filter_.getCustomShapesGeneration(); }

int FilterShapesAdapter::getMaxShapes() const { return CustomFilterShapes::kMaxShapes; }

/** A copy of the blend the cursor is on, sampled into 16 points, so the new shape sounds as the filter does now. */
ShapeItem FilterShapesAdapter::makeShapeAt(float x, float y) const {
    const auto blend =
        customfilter::blendedGridDb(filter_.getCustomShapes(), filter_.getCurrentMorphX(), filter_.getCurrentMorphY());
    ShapeItem item;
    item.curve = customfilter::curveFromGridDb(blend, 16);
    item.position = {x, y};
    return item;
}

/** Frequency across (100, 1k and 10k labelled on the log axis) and dB up, with 0 dB as the bright line. */
std::optional<CurveEditorComponent::AxisGuides> FilterShapesAdapter::getAxisGuides() const {
    CurveEditorComponent::AxisGuides guides;
    guides.x = {{CustomFilterShapes::hzToX(100.0f), "100"},
                {CustomFilterShapes::hzToX(1000.0f), "1k"},
                {CustomFilterShapes::hzToX(10000.0f), "10k"}};
    guides.y = {{CustomFilterShapes::dbToY(6.0f), "+6"},
                {CustomFilterShapes::dbToY(0.0f), "0 dB", true},
                {CustomFilterShapes::dbToY(-12.0f), "-12"},
                {CustomFilterShapes::dbToY(-24.0f), "-24"},
                {CustomFilterShapes::dbToY(-36.0f), "-36"}};
    return guides;
}

std::vector<ShapeStartFrom> FilterShapesAdapter::getStartFrom() const {
    std::vector<ShapeStartFrom> out;
    for (int i = 0; i < CustomFilterShapes::kNumPresets; ++i) {
        const auto preset = static_cast<CustomFilterShapes::Preset>(i);
        out.push_back({CustomFilterShapes::presetName(preset), CustomFilterShapes::presetCurve(preset)});
    }
    return out;
}

/** Periodic draw shapes swing between -18 dB and +6 dB, three cycles across the audible range. */
ShapeDrawRange FilterShapesAdapter::getDrawRange() const {
    return {CustomFilterShapes::dbToY(-18.0f), CustomFilterShapes::dbToY(6.0f), 3};
}

CustomFilterPerNote FilterShapesAdapter::getPerNote() const { return filter_.getCustomPerNote(); }

/** The settings are part of the module's extra state, so beginEdit/endEdit record them like a shape edit. */
void FilterShapesAdapter::setPerNote(const CustomFilterPerNote& perNote) { filter_.setCustomPerNote(perNote); }

PitchInput FilterShapesAdapter::pitchInput() const {
    if (!synth::isProcessorPoly(&filter_))
        return PitchInput::NotPoly;
    return hooks_.state ? hooks_.state() : PitchInput::Connected;
}

/** The mode write and the cable are one structural undo step, so one undo takes both back. */
bool FilterShapesAdapter::setPerNoteConnectingPitch(const CustomFilterPerNote& perNote) {
    const bool wantsPitch =
        perNote.mode == CustomFilterPerNote::Mode::ByHand || perNote.mode == CustomFilterPerNote::Mode::ByPitch;
    if (!wantsPitch || undo_ == nullptr || !hooks_.connect || pitchInput() != PitchInput::Missing)
        return ShapeListAdapter::setPerNoteConnectingPitch(perNote);
    undo_->recordStructuralChange(graph_, [this, &perNote] {
        filter_.setCustomPerNote(perNote);
        hooks_.connect();
    });
    return true;
}

juce::String FilterShapesAdapter::getWindowTitle() const { return "Filter shapes: " + moduleName_; }

float FilterShapesAdapter::getReferenceLevel() const { return CustomFilterShapes::dbToY(0.0f); }

} // namespace synth::ui
