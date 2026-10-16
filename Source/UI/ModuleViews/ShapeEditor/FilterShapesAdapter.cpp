// FilterShapesAdapter.cpp -- the Filter's Custom shapes behind the generic shape list interface.
#include "FilterShapesAdapter.h"
#include "Modules/Filter/CustomFilterBlend.h"

namespace synth::ui {

namespace {
using synth::CustomFilterShape;
using synth::CustomFilterShapes;

ShapeItem toItem(const CustomFilterShape& shape) { return {shape.name, shape.curve, shape.position}; }
} // namespace

FilterShapesAdapter::FilterShapesAdapter(FilterModule& module, AppUndoManager* undo, juce::AudioProcessorGraph& graph,
                                         juce::AudioProcessorGraph::NodeID nodeId, juce::String moduleName)
    : module_(module)
    , undo_(undo)
    , graph_(graph)
    , nodeId_(nodeId)
    , moduleName_(std::move(moduleName)) {}

std::vector<ShapeItem> FilterShapesAdapter::getShapes() const {
    std::vector<ShapeItem> out;
    for (const auto& shape : module_.getCustomShapes().shapes)
        out.push_back(toItem(shape));
    return out;
}

/** The module sanitises what it is given (names, positions, curves, the shape cap) and hands the audio thread a
    new table. */
void FilterShapesAdapter::setShapes(const std::vector<ShapeItem>& shapes) {
    CustomFilterShapes list;
    for (const auto& item : shapes)
        list.shapes.push_back({item.name, item.curve, item.position});
    module_.setCustomShapes(list);
}

int FilterShapesAdapter::getGeneration() const { return module_.getCustomShapesGeneration(); }

int FilterShapesAdapter::getMaxShapes() const { return CustomFilterShapes::kMaxShapes; }

/** Undo replays the module's extra state, so an edit is the state before and after; an edit that left the state
    as it was records nothing (AppUndoManager::recordNodeExtraStateChange). A begin inside an open edit is ignored. */
void FilterShapesAdapter::beginEdit() {
    if (editOpen_)
        return;
    editOpen_ = true;
    before_ = module_.getExtraState();
}

void FilterShapesAdapter::endEdit() {
    if (!editOpen_)
        return;
    editOpen_ = false;
    if (undo_ != nullptr)
        undo_->recordNodeExtraStateChange(graph_, nodeId_, before_, module_.getExtraState());
}

/** A copy of the blend the cursor is on, sampled into 16 points, so the new shape sounds as the filter does now. */
ShapeItem FilterShapesAdapter::makeShapeAt(float x, float y) const {
    const auto blend =
        customfilter::blendedGridDb(module_.getCustomShapes(), module_.getCurrentMorphX(), module_.getCurrentMorphY());
    ShapeItem item;
    item.curve = customfilter::curveFromGridDb(blend, 16);
    item.position = {x, y};
    return item;
}

juce::Point<float> FilterShapesAdapter::getCursor() const {
    return {module_.getCurrentMorphX(), module_.getCurrentMorphY()};
}

void FilterShapesAdapter::setCursor(juce::Point<float> position) {
    if (auto* x = findParameterByID(&module_, "morphX"))
        x->setValueNotifyingHost(x->convertTo0to1(position.x));
    if (auto* y = findParameterByID(&module_, "morphY"))
        y->setValueNotifyingHost(y->convertTo0to1(position.y));
}

/** The cursor is two parameters, so a drag is a host gesture on both (automation records it) and an undo step
    of the graph around it, the way a knob drag on the card is. */
void FilterShapesAdapter::beginCursorGesture() {
    if (cursorGestureOpen_)
        return;
    cursorGestureOpen_ = true;
    if (undo_ != nullptr)
        undo_->captureBeforeState(graph_);
    for (const char* id : {"morphX", "morphY"})
        if (auto* p = findParameterByID(&module_, id))
            p->beginChangeGesture();
}

void FilterShapesAdapter::endCursorGesture() {
    if (!cursorGestureOpen_)
        return;
    cursorGestureOpen_ = false;
    for (const char* id : {"morphX", "morphY"})
        if (auto* p = findParameterByID(&module_, id))
            p->endChangeGesture();
    if (undo_ != nullptr)
        undo_->pushSnapshotFromCapture(graph_);
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

CustomFilterPerNote FilterShapesAdapter::getPerNote() const { return module_.getCustomPerNote(); }

/** The settings are part of the module's extra state, so beginEdit/endEdit record them like a shape edit. */
void FilterShapesAdapter::setPerNote(const CustomFilterPerNote& perNote) { module_.setCustomPerNote(perNote); }

juce::String FilterShapesAdapter::getWindowTitle() const { return "Filter shapes: " + moduleName_; }

float FilterShapesAdapter::getReferenceLevel() const { return CustomFilterShapes::dbToY(0.0f); }

} // namespace synth::ui
