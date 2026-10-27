#pragma once

// CanvasEdgeScrollDriver.h
//
// GraphEditor's collaborator for "drag to the edge of the canvas to scroll it" (CanvasEdgeScroller.h is its timer,
// CanvasEdgeScroll.h its speed curve). It serves two kinds of drag:
//  - a card moved on the canvas (a module or a collapsed macro): after each pan the card gets a synthetic mouseDrag at
//    the unmoved pointer, so it takes the same path as a real one and keeps tracking under the pointer;
//  - a library or sound-browser drag onto the canvas: the landing ghost is re-aimed with a synthetic itemDragMove.
// GraphEditor holds one (getEdgeScroll()); it is a friend because it pans the view. See docs/layout/layout.md
// "Edge scroll while dragging".

#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScroller.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

class GraphEditor;

namespace synth::ui {

class CanvasEdgeScrollDriver {
public:
    explicit CanvasEdgeScrollDriver(GraphEditor& editor);

    /** Arms the scroll for a card body drag that just started; `pressInCard` is where the press landed. */
    void beginCardDrag(juce::Component& card, juce::Point<int> pressInCard);
    /** Arms it for a library or sound-browser drag that entered the canvas. */
    void beginLibraryDrag(const juce::DragAndDropTarget::SourceDetails& details);
    /** Disarms it: mouse up, drop, exit, or a cancelled drag. Safe when nothing is armed. */
    void end() { scroller_.end(); }
    bool isActive() const noexcept { return scroller_.isActive(); }

    /** Test seams: the pointer as the canvas sees it (canvas-local), replacing the real mouse and the "button is
     *  still down" check; and one scroller tick without the 30 Hz timer. */
    void setPointerForTest(std::function<juce::Point<float>()> pointerInCanvasView);
    void tickForTest() { scroller_.tick(); }

private:
    juce::Point<float> pointerInView() const;
    bool panBy(juce::Point<float> screenDelta);

    GraphEditor& editor_;
    CanvasEdgeScroller scroller_;
    std::function<juce::Point<float>()> testPointer_;
    std::optional<juce::DragAndDropTarget::SourceDetails> libraryDrag_;
};

} // namespace synth::ui
