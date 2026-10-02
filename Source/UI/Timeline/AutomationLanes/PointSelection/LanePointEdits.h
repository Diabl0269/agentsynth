#pragma once

#include "LanePointSelection.h"
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

// Copied points, each as its distance from the earliest copied beat. Lives on the lane pool so it outlives any one
// editor.
struct LanePointClipboard {
    struct Entry {
        double offset = 0.0;
        double value = 0.0;
        float tension = 0.0f;
        int curve = 0;
    };
    std::vector<Entry> entries;
    bool isEmpty() const noexcept { return entries.empty(); }
};

LanePointClipboard copyPoints(const std::vector<LaneBreakpoint>& selected);
// The clipboard laid down with its earliest point at `anchorBeat`, values clamped into [minValue, maxValue].
std::vector<LaneBreakpoint> pastedPoints(const LanePointClipboard& clipboard, double anchorBeat, double minValue,
                                         double maxValue);

} // namespace synth::ui
