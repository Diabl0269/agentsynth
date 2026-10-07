#pragma once

// GraphEditorPaintMemo.h
//
// Per-paint memo of the painted macro borders, and the canvas work counters that guard it. Kept out of
// GraphEditor.h on purpose (docs/layout/rendering.md#per-frame-work-does-not-grow-with-the-patch).

#include <juce_graphics/juce_graphics.h>
#include <unordered_map>

class GraphEditor;

namespace graph_editor_paint {

/** Message thread only. While alive, GraphEditor::paintedMacroHullBounds on `editor` computes each border once. */
class HullMemoScope {
public:
    explicit HullMemoScope(const GraphEditor& editor);
    ~HullMemoScope();
    HullMemoScope(const HullMemoScope&) = delete;
    HullMemoScope& operator=(const HullMemoScope&) = delete;

private:
    friend class ::GraphEditor;
    const GraphEditor& editor_;
    HullMemoScope* previous_ = nullptr;
    std::unordered_map<juce::String, juce::Rectangle<int>> hulls_;
};

/** Counts since the last reset (test seam): borders computed, whole-graph node scans by uuid or node id,
 *  whole-cable-set scans, and the cables and macro borders a paint actually drew. */
struct WorkCounters {
    int hullComputations = 0;
    int nodeScans = 0;
    int cableScans = 0; // a ConnectionIndex built: one pass over every cable (ModMatrixEndpoints.h)
    int cablesPainted = 0;
    int hullsPainted = 0;
};

/** Message thread only. */
WorkCounters& workCounters() noexcept;

} // namespace graph_editor_paint
