// Concern (docs/control/midi-remote-ui.md#surface-centre): marquee-select / pan -- a press-drag
// over EMPTY grid space (a press that lands on a cell never reaches here: ControllerSurfaceCell
// is a real juce::Component covering its own bounds and handles its own mouseDown, so this
// component's own mouseDown only ever fires for a click that missed every cell). Shift picks
// which gesture a press can become, decided once at mouseDown and latched for the whole drag
// (marqueeArmed_) so a modifier changing mid-drag -- which a real OS mostly can't deliver
// mid-gesture anyway -- can never flip one gesture into the other partway through:
//  - Shift held: a marquee (Cmd+Shift additive, matching GraphEditor's own
//    "isCommandDown() || isCtrlDown()" marquee-additive test) -- mirrors ControllerSurfaceCell's
//    own click-vs-drag debounce (isDragging_/dragStartMouse_): a Shift-press with no movement is a
//    no-op (there is no defined "Shift-click empty space" gesture), a real drag paints the band and
//    selects on release.
//  - Otherwise: pan, latched by the same click-vs-drag debounce (GraphEditor::
//    pendingEmptyCanvasClick's own pattern) -- a press with no movement clears the selection
//    instead, so panning never wipes it out from under an accidental micro-drag.

#include "ControllerSurfaceComponent.h"

#include <algorithm>

namespace synth::ui {

std::vector<juce::String> ControllerSurfaceComponent::collectMarqueeHits() const {
    std::vector<juce::String> hits;
    for (const auto* cell : cells_)
        if (cell->getBounds().intersects(marqueeRect_))
            hits.push_back(cell->getControlId());
    return hits;
}

void ControllerSurfaceComponent::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus(); // so Esc/Delete reach keyPressed() even for a plain click that clears
    marqueeActive_ = false;
    marqueeRect_ = {};

    if (event.mods.isShiftDown()) {
        marqueeArmed_ = true;
        pendingEmptyClick_ = false;
        marqueeAdditive_ = event.mods.isCommandDown() || event.mods.isCtrlDown();
        marqueeAnchor_ = content_.getLocalPoint(this, event.position).roundToInt();
        return;
    }

    marqueeArmed_ = false;
    pendingEmptyClick_ = true;
    lastPanMouse_ = event.position;
}

void ControllerSurfaceComponent::mouseDrag(const juce::MouseEvent& event) {
    if (marqueeArmed_) {
        const auto previousRect = marqueeRect_;
        marqueeRect_ = juce::Rectangle<int>(marqueeAnchor_, content_.getLocalPoint(this, event.position).roundToInt());
        marqueeActive_ = true;
        // Only the changed region -- the union of where the marquee WAS and where it is now,
        // expanded by the border's stroke width (Source/UI/CLAUDE.md's repaint rule: never repaint
        // the whole surface for a drag that only moved a few pixels). content_'s own repaint, not
        // this component's -- the band is painted in content_'s Content::paintOverChildren()
        // (ControllerSurfaceView.cpp), in content-local space so it stays locked to the cells it is
        // selecting under pan/zoom.
        content_.repaint(previousRect.getUnion(marqueeRect_).expanded(2));
        return;
    }

    // Pan: a plain screen-space delta added straight onto panOffset_ (panOffset_ is applied AFTER
    // the zoom scale in updateTransform()'s transform, so it is already in screen pixels regardless
    // of zoomLevel_ -- see ControllerSurfaceView.cpp).
    pendingEmptyClick_ = false;
    panOffset_ += (event.position - lastPanMouse_);
    lastPanMouse_ = event.position;
    updateTransform();
}

void ControllerSurfaceComponent::mouseUp(const juce::MouseEvent&) {
    if (marqueeArmed_) {
        marqueeArmed_ = false;
        if (!marqueeActive_)
            return; // a Shift-press with no drag -- no defined gesture, selection untouched

        auto hits = collectMarqueeHits();
        if (marqueeAdditive_) {
            std::vector<juce::String> merged = selectedIds_;
            for (auto& id : hits)
                if (std::find(merged.begin(), merged.end(), id) == merged.end())
                    merged.push_back(std::move(id));
            setSelectionInternal(std::move(merged));
        } else {
            setSelectionInternal(std::move(hits));
        }

        const auto eraseRect = marqueeRect_;
        marqueeActive_ = false;
        marqueeRect_ = {};
        content_.repaint(eraseRect.expanded(2));
        return;
    }

    // A plain press that never actually panned -- clears the selection.
    if (pendingEmptyClick_) {
        pendingEmptyClick_ = false;
        if (!selectedIds_.empty())
            setSelectionInternal({});
    }
}

} // namespace synth::ui
