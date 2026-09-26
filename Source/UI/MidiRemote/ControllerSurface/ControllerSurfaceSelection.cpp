// Concern: FRO270 (docs/control/midi-remote-ui.md#surface-centre) -- the surface's own selection
// set: plain/shift/cmd click on a cell, and the two public setters the panel uses to apply or
// restore a selection from outside a click (a profile switch, Detect, Assign, an undo/redo).
// Marquee selection lives in ControllerSurfaceMarquee.cpp -- it ends by calling
// setSelectionInternal() below too, so this is the one place selectedIds_ is ever written.

#include "ControllerSurfaceComponent.h"

#include <algorithm>

namespace synth::ui {

// The one place selectedIds_ is written: de-duplicates (setSelectedControlIds() is a public entry
// point with no promise its caller didn't repeat an id), applies the per-cell highlight, and
// notifies the panel unless the caller is about to notify some other way itself.
void ControllerSurfaceComponent::setSelectionInternal(std::vector<juce::String> ids, bool notify) {
    std::vector<juce::String> unique;
    for (auto& id : ids)
        if (std::find(unique.begin(), unique.end(), id) == unique.end())
            unique.push_back(std::move(id));
    selectedIds_ = std::move(unique);

    for (auto* cell : cells_)
        cell->setSelected(std::find(selectedIds_.begin(), selectedIds_.end(), cell->getControlId()) !=
                          selectedIds_.end());

    if (notify && onSelectionChanged)
        onSelectionChanged(selectedIds_);
}

void ControllerSurfaceComponent::setSelectedControlId(const juce::String& controlId) {
    setSelectionInternal(controlId.isEmpty() ? std::vector<juce::String>{} : std::vector<juce::String>{controlId});
}

void ControllerSurfaceComponent::setSelectedControlIds(const std::vector<juce::String>& ids) {
    std::vector<juce::String> present;
    for (const auto& id : ids)
        if (findCellForTest(id) != nullptr)
            present.push_back(id);
    setSelectionInternal(std::move(present));
}

// A cell's mouseDown (ControllerSurfaceCell::mouseDown -> onSelected). `mods` picks which of the
// three click behaviours (docs/control/midi-remote-ui.md#surface-centre) applies:
//  - cmd: toggles `controlId` in/out of the selection.
//  - shift: adds `controlId` if it isn't already selected; never removes one (there is no natural
//    linear order on a 2D grid to "extend" a range along, so shift and cmd differ only in that
//    shift never shrinks the selection).
//  - plain: replaces the selection with just `controlId` -- UNLESS `controlId` is already part of
//    a multi-selection, in which case the PRESS alone must not shrink it: it may be the start of a
//    group drag (ControllerSurfaceGroupDrag.cpp), and a plain click that turns out not to have
//    dragged leaves the group selected exactly as it was, rather than collapsing on the very press
//    that begins the drag.
void ControllerSurfaceComponent::handleCellSelected(const juce::String& controlId, const juce::ModifierKeys& mods) {
    std::vector<juce::String> ids = selectedIds_;
    const bool alreadyIn = std::find(ids.begin(), ids.end(), controlId) != ids.end();

    if (mods.isCommandDown()) {
        if (alreadyIn)
            ids.erase(std::remove(ids.begin(), ids.end(), controlId), ids.end());
        else
            ids.push_back(controlId);
    } else if (mods.isShiftDown()) {
        if (!alreadyIn)
            ids.push_back(controlId);
    } else if (alreadyIn && ids.size() > 1) {
        // Selection unchanged -- see the doc comment above.
    } else {
        ids = {controlId};
    }

    setSelectionInternal(std::move(ids));
    // Delete/Backspace/Esc must reach keyPressed() below -- grab focus at the moment of selection,
    // per the header's own doc comment on onDeleteControlsRequested.
    grabKeyboardFocus();
}

} // namespace synth::ui
