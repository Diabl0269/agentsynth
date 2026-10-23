// LfoShapesAdapter.cpp -- the LFO's Custom shapes behind the generic shape list interface.
#include "LfoShapesAdapter.h"
#include "Modules/Lfo/LfoShapeBlender.h"
#include "Modules/Shapes/CurveSimplify.h"
#include "UI/ModuleViews/CurveEditor/CurveGridNoteLength.h"
#include <set>

namespace synth::ui {

namespace {
constexpr float kToleranceY = 0.01f; // of the 0..1 value range

const char* const kPresetNames[] = {"Triangle",  "Ramp Up",   "Ramp Down", "Square",
                                    "Pulse 1/4", "Steps (4)", "Soft Sine"};
static_assert(sizeof(kPresetNames) / sizeof(kPresetNames[0]) == 7, "one name per LfoCustomWave::Preset");

/** A curve through as few samples of `table` as keep it within kToleranceY of it: the ends, every corner and bend. */
LfoCustomWave curveFromTable(const LfoCustomWave::Table& table) {
    const std::vector<float> values(table.begin(), table.end());
    std::set<int> chosen{0, LfoCustomWave::kTableSize};
    synth::shapes::addPointsWhereLinesMiss(chosen, values, LfoCustomWave::kMaxPoints, kToleranceY);
    LfoCustomWave wave;
    for (const int k : chosen)
        wave.points.push_back(
            {(float)k / (float)LfoCustomWave::kTableSize, juce::jlimit(0.0f, 1.0f, values[(size_t)k]), 0.0f});
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

/** A copy of the blend the cursor is on, kept to the points it needs, so the new shape plays as the LFO does now. */
ShapeItem LfoShapesAdapter::makeShapeAt(float x, float y) const {
    const auto table =
        LfoShapeBlender::blendedTable(lfo_.getCustomShapes(), lfo_.getCurrentMorphX(), lfo_.getCurrentMorphY());
    ShapeItem item;
    item.curve = curveFromTable(table);
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
