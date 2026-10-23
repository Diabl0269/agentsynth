// PointEditorDuplicate.h -- where Cmd+D puts the copies of selected points, in the automation lanes and the curve
// editor alike. A Preferences choice (Timeline > Duplicate points), process-wide like the Animations mode.
#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace synth::ui {

enum class DuplicatePlacement {
    afterLast,       // the first copy one tick after the last selected point; the copies keep their spacing
    onePointSpacing, // the first copy one point-spacing (the gap between the last two selected) after the last
    nextGridLine     // the copies shifted by whole grid steps so the first copy sits past the last selected point
};

constexpr const char* kDuplicatePlacementKey = "duplicatePointsPlacement";

/** The process-wide choice (default afterLast); set at startup from the user settings and when the preference
 *  changes. Message thread only. */
DuplicatePlacement duplicatePlacement();
void setDuplicatePlacement(DuplicatePlacement placement);

/** The persisted spelling; an unknown string reads as the default. */
juce::String duplicatePlacementToString(DuplicatePlacement placement);
DuplicatePlacement duplicatePlacementFromString(const juce::String& text);

/** One tick in beats: the finest resolution the timeline keeps a beat to (its MIDI export's 960 per quarter note). */
double tickBeats();

struct DuplicateSteps {
    /** One grid step, or 0 when snap is off. */
    double grid = 0.0;
    /** How far past a lone selected point its copy goes. */
    double singleStep = 0.0;
    /** The smallest step the editor has (one tick); a copy starts this far past the last point. */
    double tick = 0.0;
};

/** Where the first copy starts, for the selected points' positions (ascending, at least one). The copies keep their
 *  spacing from the first copy on. */
double duplicateAnchor(DuplicatePlacement placement, const std::vector<double>& positions, const DuplicateSteps& steps);

} // namespace synth::ui
