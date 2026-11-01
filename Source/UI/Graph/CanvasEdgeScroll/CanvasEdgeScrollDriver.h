#pragma once

// CanvasEdgeScrollDriver.h
//
// GraphEditor's collaborator for "drag to the edge of the canvas to scroll it" (CanvasEdgeScroller.h is its timer,
// CanvasEdgeScroll.h its speed curve). It serves two kinds of drag:
//  - a card moved on the canvas (a module or a collapsed macro): after each pan the card gets a synthetic mouseDrag at
//    the unmoved pointer, so it takes the same path as a real one and keeps tracking under the pointer;
//  - a cable dragged from a jack, a selection box, or an expanded macro moved by its name chip: the drag is re-aimed
//    at the unmoved pointer with a synthetic dragConnection / canvas mouseDrag;
//  - a library or sound-browser drag onto the canvas: the landing ghost is re-aimed with a synthetic itemDragMove.
// GraphEditor holds one (getEdgeScroll()); it is a friend because it pans the view. See docs/layout/layout.md
// "Edge scroll while dragging".

#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScroller.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

class GraphEditor;

namespace synth::ui {

class CanvasEdgeScrollDriver : private juce::MouseListener {
public:
    explicit CanvasEdgeScrollDriver(GraphEditor& editor);
    ~CanvasEdgeScrollDriver() override;

    /** Arms the scroll for a card body drag that just started; `pressInCard` is where the press landed. */
    void beginCardDrag(juce::Component& card, juce::Point<int> pressInCard);
    /** Arms it for a cable drag that just began at `screenPos`: after each pan GraphEditor::dragConnection re-aims it
     * at the pointer.
     */
    void beginCableDrag(juce::Point<int> screenPos);
    /** Arms it for a drag the canvas itself owns (the selection box, an expanded macro's name chip): after each pan
     *  GraphEditor::mouseDrag gets a synthetic event at the pointer, so the box or the macro keeps tracking it. */
    void beginCanvasDrag();
    /** Arms it for a library or sound-browser drag that entered the canvas. */
    void beginLibraryDrag(const juce::DragAndDropTarget::SourceDetails& details);
    /** Where the pointer is, from the mouse event a drag handler is handling: call it first thing in the handler,
     * before the card moves (an event's screen position follows its component). Test mode injects events that the OS
     * cursor never sees, so the pointer is the last one a drag reported, not the cursor; the cursor stands in until
     * then. Ignored while a scroll tick re-sends a synthetic drag. */
    void notePointer(const juce::MouseEvent& e);
    /** Disarms it: mouse up, drop, exit, or a cancelled drag. Safe when nothing is armed. */
    void end() { scroller_.end(); }
    bool isActive() const noexcept { return scroller_.isActive(); }

    /** Test seams: the pointer as the canvas sees it (canvas-local), replacing the real mouse and the "button is
     *  still down" check; and one scroller tick without the 30 Hz timer. */
    void setPointerForTest(std::function<juce::Point<float>()> pointerInCanvasView);
    void tickForTest() { scroller_.tick(); }

private:
    // Events on the canvas itself are heard through a global listener (the canvas does not move, so the screen position
    // stays true however far it has scrolled); the cards' own drags report through notePointer.
    void mouseDown(const juce::MouseEvent& e) override { noteIfOnCanvas(e); }
    void mouseDrag(const juce::MouseEvent& e) override { noteIfOnCanvas(e); }
    void mouseMove(const juce::MouseEvent& e) override { noteIfOnCanvas(e); }
    void noteIfOnCanvas(const juce::MouseEvent& e);
    juce::Point<float> pointerInView() const;
    bool panBy(juce::Point<float> screenDelta);

    GraphEditor& editor_;
    CanvasEdgeScroller scroller_;
    std::function<juce::Point<float>()> testPointer_;
    std::optional<juce::Point<float>> lastScreenPosition_; // none until a drag reports one
    bool inSyntheticDrag_ = false;
    std::optional<juce::DragAndDropTarget::SourceDetails> libraryDrag_;
};

} // namespace synth::ui
