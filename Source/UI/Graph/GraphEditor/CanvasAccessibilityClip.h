// CanvasAccessibilityClip.h
//
// FRO300: a canvas card panned or scrolled outside GraphEditor::getVisibleCanvasRect() (or
// covered by the bottom dock shrinking that rect) is still a live JUCE child of
// GraphEditor::GraphContentComponent -- nothing about being off-screen removes it from the
// accessibility tree, so VoiceOver keeps announcing cards the user can't see or reach. JUCE's
// Component::isAccessible() walks the parent chain (juce_Component.cpp), so marking the CARD
// itself inaccessible also hides every one of its child widgets (knobs, the title editor, ...)
// without touching them directly -- confirmed by the child-slider assertion in
// GraphEditorViewportTests.cpp.
//
// Only applies to components with bounds-limited canvas meaning (a card): a full-canvas overlay
// or cable layer would go off the visible rect on one edge while still covering the rest of the
// canvas, and must never be hidden by this. GraphContentComponent parents exactly two such card
// kinds -- ModuleComponent and MacroCardComponent -- so callers pass one OwnedArray of each
// rather than this header walking `content`'s children by dynamic_cast.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace detail {

// Applies `visibleCanvasRect` (canvas-space, from GraphEditor::getVisibleCanvasRect()) to one
// array of cards: a card entirely outside it is marked inaccessible, one at least partially
// inside stays/becomes accessible. Guarded per-card by `isAccessible() != desired` -- this runs
// on every wheel/pan/drag frame via updateTransform(), and setAccessible() invalidates the
// native accessibility handler, so an unconditional call would tear that handler down every
// frame even when nothing crossed the boundary.
template <typename CardArray>
inline void clipCardArrayToVisibleCanvas(CardArray& cards, juce::Rectangle<float> visibleCanvasRect) {
    for (auto* card : cards) {
        if (card == nullptr)
            continue;
        const bool desired = visibleCanvasRect.intersects(card->getBounds().toFloat());
        if (card->isAccessible() != desired)
            card->setAccessible(desired);
    }
}

// Convenience overload for GraphEditor's two card arrays (ModuleComponent + MacroCardComponent)
// together, since every call site needs both.
template <typename ModuleArray, typename MacroCardArray>
inline void applyCanvasAccessibilityClip(ModuleArray& modules, MacroCardArray& macroCards,
                                         juce::Rectangle<float> visibleCanvasRect) {
    clipCardArrayToVisibleCanvas(modules, visibleCanvasRect);
    clipCardArrayToVisibleCanvas(macroCards, visibleCanvasRect);
}

} // namespace detail
