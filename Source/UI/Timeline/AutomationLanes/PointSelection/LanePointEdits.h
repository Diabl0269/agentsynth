#pragma once

#include "LanePointSelection.h"
#include "UI/Layout/PointEditing/PointClipboard.h"
#include <vector>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

// LanePointEdits -- the doc edits a point selection makes (move, delete, paste) and the clipboard they share. Each is
// one doc mutation and, with an undo manager, one undo step. Message thread only.
namespace synth::ui {

// Non-owning; `undo` may be null (the edit then runs unrecorded).
struct LaneEditTarget {
    synth::TimelineDoc* doc = nullptr;
    AppUndoManager* undo = nullptr;
    synth::LaneId lane;
};

// Removes `removeBeats` then inserts `add` as one edit; returns false when nothing changed.
bool commitPointEdit(const LaneEditTarget& target, const std::vector<double>& removeBeats,
                     const std::vector<LaneBreakpoint>& add);

// What a copied lane point carries besides its beat and value.
struct LaneClipExtra {
    float tension = 0.0f;
    int curve = 0;
};

// Copied points, each as its distance from the earliest copied beat (the shared PointClip). Lives on the lane pool so
// it outlives any one editor.
struct LanePointClipboard : PointClip<LaneClipExtra> {};

LanePointClipboard copyPoints(const std::vector<LaneBreakpoint>& selected);
// The clipboard laid down with its earliest point at `anchorBeat`, values clamped into [minValue, maxValue].
std::vector<LaneBreakpoint> pastedPoints(const LanePointClipboard& clipboard, double anchorBeat, double minValue,
                                         double maxValue);

} // namespace synth::ui
