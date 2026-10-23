// LfoShapesAdapter.cpp -- the LFO's Custom shapes behind the generic shape list interface.
#include "LfoShapesAdapter.h"
#include "Modules/Lfo/LfoShapeBlender.h"
#include "UI/ModuleViews/CurveEditor/CurveGridNoteLength.h"

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
    : ModuleShapesAdapter(module, undo, graph, nodeId, std::move(moduleName))
    , lfo_(module) {}

/** A shape that was never named (the plain wave every LFO starts with) reads "Shape N" so a chip is never blank. */
std::vector<ShapeItem> LfoShapesAdapter::getShapes() const {
    std::vector<ShapeItem> out;
    int index = 1;
    for (const auto& shape : lfo_.getCustomShapes().shapes) {
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
    lfo_.setCustomShapes(list);
}

int LfoShapesAdapter::getGeneration() const { return lfo_.getCustomWaveGeneration(); }

int LfoShapesAdapter::getMaxShapes() const { return LfoCustomShapes::kMaxShapes; }

/** A copy of the blend the cursor is on, sampled into 64 points, so the new shape plays as the LFO does now. */
ShapeItem LfoShapesAdapter::makeShapeAt(float x, float y) const {
    const auto table =
        LfoShapeBlender::blendedTable(lfo_.getCustomShapes(), lfo_.getCurrentMorphX(), lfo_.getCurrentMorphY());
    ShapeItem item;
    item.curve = curveFromTable(table, kNewShapePoints);
    item.position = {x, y};
    return item;
}

std::vector<ShapeStartFrom> LfoShapesAdapter::getStartFrom() const {
    std::vector<ShapeStartFrom> out;
    for (int i = 0; i < 7; ++i)
        out.push_back({kPresetNames[i], LfoCustomWave::preset(static_cast<LfoCustomWave::Preset>(i))});
    return out;
}

/** The time axis is one cycle, so the grid is the LFO card's: the shared note lengths, triplets included. */
std::optional<ShapeGridOffer> LfoShapesAdapter::getGridOffer() const {
    ShapeGridOffer offer;
    offer.control = ShapeGridOffer::Control::noteLengthBox;
    offer.names = curvegrid::noteLengthNames();
    offer.title = "Shape grid";
    offer.tooltip = "Grid: snap points to a note length across the cycle (Off: free)";
    offer.gridFor = [](const juce::String& name) -> std::optional<CurveEditorComponent::CurveGrid> {
        const int divisions = curvegrid::noteLengthDivisions(name);
        if (divisions <= 0)
            return std::nullopt;
        return curvegrid::gridForDivisions(divisions);
    };
    return offer;
}

juce::String LfoShapesAdapter::getWindowTitle() const { return "LFO shapes: " + moduleName_; }

} // namespace synth::ui
