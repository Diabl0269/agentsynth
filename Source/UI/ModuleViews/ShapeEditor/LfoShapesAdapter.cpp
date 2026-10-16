// LfoShapesAdapter.cpp -- the LFO's Custom shapes behind the generic shape list interface.
#include "LfoShapesAdapter.h"
#include "Modules/Lfo/LfoShapeBlender.h"

namespace synth::ui {

namespace {
constexpr int kNewShapePoints = LfoCustomWave::kMaxPoints;

const char* const kPresetNames[] = {"Triangle",  "Ramp Up",   "Ramp Down", "Square",
                                    "Pulse 1/4", "Steps (4)", "Soft Sine"};
static_assert(sizeof(kPresetNames) / sizeof(kPresetNames[0]) == 7, "one name per LfoCustomWave::Preset");

/** A curve of `numPoints` evenly spaced samples of `table` (the first at x = 0, the last at x = 1). */
LfoCustomWave curveFromTable(const LfoCustomWave::Table& table, int numPoints) {
    LfoCustomWave wave;
    for (int p = 0; p < numPoints; ++p) {
        const float x = (float)p / (float)(numPoints - 1);
        const float pos = x * (float)LfoCustomWave::kTableSize;
        const int i0 = juce::jmin((int)pos, LfoCustomWave::kTableSize - 1);
        const float y = table[(size_t)i0] + (table[(size_t)i0 + 1] - table[(size_t)i0]) * (pos - (float)i0);
        wave.points.push_back({x, juce::jlimit(0.0f, 1.0f, y), 0.0f});
    }
    wave.sanitise();
    return wave;
}
} // namespace

LfoShapesAdapter::LfoShapesAdapter(LFOModule& module, AppUndoManager* undo, juce::AudioProcessorGraph& graph,
                                   juce::AudioProcessorGraph::NodeID nodeId, juce::String moduleName)
    : module_(module)
    , undo_(undo)
    , graph_(graph)
    , nodeId_(nodeId)
    , moduleName_(std::move(moduleName)) {}

/** A shape that was never named (the plain wave every LFO starts with) reads "Shape N" so a chip is never blank. */
std::vector<ShapeItem> LfoShapesAdapter::getShapes() const {
    std::vector<ShapeItem> out;
    int index = 1;
    for (const auto& shape : module_.getCustomShapes().shapes) {
        out.push_back(
            {shape.name.isEmpty() ? "Shape " + juce::String(index) : shape.name, shape.curve, shape.position});
        ++index;
    }
    return out;
}

/** The module sanitises what it is given (names, positions, curves, the shape cap) and hands the audio thread new
    tables. */
void LfoShapesAdapter::setShapes(const std::vector<ShapeItem>& shapes) {
    LfoCustomShapes list;
    for (const auto& item : shapes)
        list.shapes.push_back({item.name, item.curve, item.position});
    module_.setCustomShapes(list);
}

int LfoShapesAdapter::getGeneration() const { return module_.getCustomWaveGeneration(); }

int LfoShapesAdapter::getMaxShapes() const { return LfoCustomShapes::kMaxShapes; }

/** Undo replays the module's extra state, so an edit is the state before and after; an edit that left the state
    as it was records nothing (AppUndoManager::recordNodeExtraStateChange). A begin inside an open edit is ignored. */
void LfoShapesAdapter::beginEdit() {
    if (editOpen_)
        return;
    editOpen_ = true;
    before_ = module_.getExtraState();
}

void LfoShapesAdapter::endEdit() {
    if (!editOpen_)
        return;
    editOpen_ = false;
    if (undo_ != nullptr)
        undo_->recordNodeExtraStateChange(graph_, nodeId_, before_, module_.getExtraState());
}

/** A copy of the blend the cursor is on, sampled into 64 points, so the new shape plays as the LFO does now. */
ShapeItem LfoShapesAdapter::makeShapeAt(float x, float y) const {
    const auto table = LfoShapeBlender::blendedTable(module_.getCustomShapes(), module_.getCurrentMorphX(),
                                                     module_.getCurrentMorphY());
    ShapeItem item;
    item.curve = curveFromTable(table, kNewShapePoints);
    item.position = {x, y};
    return item;
}

juce::Point<float> LfoShapesAdapter::getCursor() const {
    return {module_.getCurrentMorphX(), module_.getCurrentMorphY()};
}

void LfoShapesAdapter::setCursor(juce::Point<float> position) {
    if (auto* x = findParameterByID(&module_, "morphX"))
        x->setValueNotifyingHost(x->convertTo0to1(position.x));
    if (auto* y = findParameterByID(&module_, "morphY"))
        y->setValueNotifyingHost(y->convertTo0to1(position.y));
}

/** The cursor is two parameters, so a drag is a host gesture on both (automation records it) and an undo step
    of the graph around it, the way a knob drag on the card is. */
void LfoShapesAdapter::beginCursorGesture() {
    if (cursorGestureOpen_)
        return;
    cursorGestureOpen_ = true;
    if (undo_ != nullptr)
        undo_->captureBeforeState(graph_);
    for (const char* id : {"morphX", "morphY"})
        if (auto* p = findParameterByID(&module_, id))
            p->beginChangeGesture();
}

void LfoShapesAdapter::endCursorGesture() {
    if (!cursorGestureOpen_)
        return;
    cursorGestureOpen_ = false;
    for (const char* id : {"morphX", "morphY"})
        if (auto* p = findParameterByID(&module_, id))
            p->endChangeGesture();
    if (undo_ != nullptr)
        undo_->pushSnapshotFromCapture(graph_);
}

std::vector<ShapeStartFrom> LfoShapesAdapter::getStartFrom() const {
    std::vector<ShapeStartFrom> out;
    for (int i = 0; i < 7; ++i)
        out.push_back({kPresetNames[i], LfoCustomWave::preset(static_cast<LfoCustomWave::Preset>(i))});
    return out;
}

juce::String LfoShapesAdapter::getWindowTitle() const { return "LFO shapes: " + moduleName_; }

} // namespace synth::ui
