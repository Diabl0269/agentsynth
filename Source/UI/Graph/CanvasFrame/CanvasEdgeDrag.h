#pragma once

// CanvasEdgeDrag.h
//
// The left/top half of the growing canvas. Nothing may sit left of or above the canvas origin (a JUCE child at a
// negative position is unpainted and unclickable), so a drag that pushes its cards past the origin is held at the
// edge, and the drop slides the rest of the patch right/down by the overshoot instead
// (GraphEditor::slidePatchForEdgeDrop). This class is the drag's bookkeeping: what moves, where its union started,
// and how far past the edge the pointer has gone. See docs/layout/layout.md "Canvas frame".

#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

class CanvasEdgeDrag {
public:
    /** Arms a drag of the layout units `movingKeys`, whose union starts at `movingUnion`; `floor` is the smallest
     *  top-left that union may reach (an open hull keeps its port overhang inside the canvas). */
    void begin(juce::Rectangle<int> movingUnion, juce::Point<int> floor, std::set<juce::String> movingKeys);
    void reset() { *this = {}; }

    /** The drag delta with the moving union held at the floor; remembers how far past it `raw` went. A raw delta
     *  passes through untouched when no drag is armed. */
    juce::Point<int> clampDelta(juce::Point<int> raw);

    /** The last overshoot, rounded up to the grid so a slide keeps every card on it, then cleared. */
    juce::Point<int> takeOvershoot();

    bool isMoving(const juce::String& unitKey) const { return movingKeys_.count(unitKey) != 0; }

private:
    bool armed_ = false;
    juce::Rectangle<int> startUnion_;
    juce::Point<int> floor_;
    juce::Point<int> overshoot_;
    std::set<juce::String> movingKeys_;
};
