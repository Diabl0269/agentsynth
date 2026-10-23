#include "PointEditorDuplicate.h"
#include "Timeline/MidiClipFile.h"
#include <cmath>

namespace synth::ui {

namespace {
DuplicatePlacement& placementValue() {
    static DuplicatePlacement placement = DuplicatePlacement::afterLast;
    return placement;
}
} // namespace

double tickBeats() { return 1.0 / (double)synth::MidiClipFile::kExportPpq; }

DuplicatePlacement duplicatePlacement() { return placementValue(); }

void setDuplicatePlacement(DuplicatePlacement placement) { placementValue() = placement; }

juce::String duplicatePlacementToString(DuplicatePlacement placement) {
    switch (placement) {
    case DuplicatePlacement::onePointSpacing:
        return "spacing";
    case DuplicatePlacement::nextGridLine:
        return "grid";
    case DuplicatePlacement::afterLast:
        break;
    }
    return "afterLast";
}

DuplicatePlacement duplicatePlacementFromString(const juce::String& text) {
    if (text == "spacing")
        return DuplicatePlacement::onePointSpacing;
    if (text == "grid")
        return DuplicatePlacement::nextGridLine;
    return DuplicatePlacement::afterLast;
}

// A lone point has no spacing or span, so every choice puts its copy one step on. With snap off the grid choice is
// the default one. The grid choice goes to the first grid step strictly past the last point: a span that is a whole
// number of steps would otherwise put the first copy on top of the last point.
double duplicateAnchor(DuplicatePlacement placement, const std::vector<double>& positions,
                       const DuplicateSteps& steps) {
    const double first = positions.front();
    const double last = positions.back();
    if (positions.size() < 2)
        return last + steps.singleStep;
    if (placement == DuplicatePlacement::onePointSpacing)
        return last + (last - positions[positions.size() - 2]);
    if (placement == DuplicatePlacement::nextGridLine && steps.grid > 0.0)
        return first + (std::floor((last - first) / steps.grid + 1e-9) + 1.0) * steps.grid;
    return last + steps.tick;
}

} // namespace synth::ui
