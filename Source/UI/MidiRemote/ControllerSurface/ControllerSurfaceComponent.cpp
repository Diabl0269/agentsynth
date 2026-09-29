// ControllerSurfaceComponent.cpp (docs/control/midi-remote-ui.md#surface-centre): builds
// and positions the grid of ControllerSurfaceCells, forwards activity, and owns the grid-level
// paint/keyboard plumbing shared by every concern. Selection (click/shift/cmd) lives in
// ControllerSurfaceSelection.cpp, the empty-space marquee in ControllerSurfaceMarquee.cpp, and
// drag-to-move (single or group) in ControllerSurfaceGroupDrag.cpp -- this file only wires each
// cell's callbacks to those units' handlers.
//
// MID-GESTURE REBUILD HAZARD (Source/UI/CLAUDE.md's rebuild rule): onControlsMoved is fired from
// inside a cell's own mouseUp() call stack (ControllerSurfaceCell::mouseUp -> onDragEnded ->
// handleCellDragEnded -> onControlsMoved). If THIS component reacted by synchronously calling
// setControls() (which clears cells_, destroying the very cell whose mouseUp is still on the
// stack), the cell would finish its own mouseUp on a freed `this` -- the exact crash class
// GraphEditor::cancelLiveDragGestures() exists to avoid on the canvas side. So this component does
// nothing destructive in response to its own onControlsMoved/onDragEnded: it only computes the
// clamped new grid position(s) and invokes the callback, then returns. The caller
// (MidiRemotePanelComponent, one level up) is the one that owns the deferral -- it defers its OWN
// setControls() rebuild via juce::MessageManager::callAsync after receiving onControlsMoved -- so by the time cells_ is
// ever rebuilt, the gesture's call stack has long since unwound.

#include "ControllerSurfaceComponent.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <algorithm>

namespace synth::ui {

ControllerSurfaceComponent::ControllerSurfaceComponent()
    : content_(*this) {
    // Delete/Backspace/Esc must reach THIS component's keyPressed(), not go looking for a focused
    // cell -- cells are mouse-inert display widgets and never take focus themselves.
    setWantsKeyboardFocus(true);

    // content_ never handles a click itself -- an empty-space press must reach THIS
    // component's mouseDown (pan/marquee/deselect), exactly the "fallback clicks to parent" wiring
    // GraphEditor uses for its own content child. A cell (added to content_ below) still gets its
    // own press first, since a child's own interception is unaffected by its parent's.
    addAndMakeVisible(content_);
    content_.setInterceptsMouseClicks(false, true);
    updateTransform();
}

ControllerSurfaceComponent::~ControllerSurfaceComponent() = default;

// Switching to a different controller drops any control selection (matches the
// behaviour of the panel's own selectedControlId_.clear()); rebuilding the SAME profile (a live
// refresh -- Detect, a move, a delete, an undo/redo) instead prunes the existing selection down to
// whatever ids still exist, so the caller doesn't have to re-apply it after every mutation.
void ControllerSurfaceComponent::setControls(const juce::String& profileId, const std::vector<CellModel>& cells) {
    if (profileId != profileId_) {
        selectedIds_.clear();
        restoreOrResetView(profileId);
    }
    profileId_ = profileId;
    cells_.clear();

    for (const auto& cellModel : cells) {
        auto* cell = cells_.add(new ControllerSurfaceCell());
        cell->configure(cellModel.control, cellModel.assignmentLabel, cellModel.isWarning, cellModel.isMapped,
                        cellModel.initialValue);

        const int x = kCellMargin + cellModel.control.layout.col * (kCellSize + kCellMargin);
        const int y = kCellMargin + cellModel.control.layout.row * (kCellSize + kCellMargin);
        cell->setBounds(x, y, kCellSize, kCellSize);
        content_.addAndMakeVisible(cell);

        const juce::String controlId = cellModel.control.id;
        cell->onSelected = [this, controlId](const juce::ModifierKeys& mods) { handleCellSelected(controlId, mods); };
        cell->onDraggedByCells = [this, controlId](int dCols, int dRows) {
            handleCellDragged(controlId, dCols, dRows);
        };
        cell->onDragEnded = [this, controlId]() { handleCellDragEnded(controlId); };

        cell->setSelected(std::find(selectedIds_.begin(), selectedIds_.end(), controlId) != selectedIds_.end());
        if (controlId == pulseControlId_)
            cell->setDetectPulse(pulseSinceMs_);
    }

    std::vector<juce::String> pruned;
    for (const auto& id : selectedIds_)
        if (findCellForTest(id) != nullptr)
            pruned.push_back(id);
    if (pruned.size() != selectedIds_.size()) {
        selectedIds_ = std::move(pruned);
        if (onSelectionChanged)
            onSelectionChanged(selectedIds_);
    }
}

void ControllerSurfaceComponent::setDetectPulseControlId(const juce::String& controlId) {
    if (controlId == pulseControlId_)
        return;
    pulseControlId_ = controlId;
    pulseSinceMs_ = controlId.isEmpty() ? 0.0 : juce::Time::getMillisecondCounterHiRes();
    for (auto* cell : cells_)
        cell->setDetectPulse(cell->getControlId() == controlId ? pulseSinceMs_ : 0.0);
}

void ControllerSurfaceComponent::flashControl(const juce::String& controlId) {
    for (auto* cell : cells_)
        if (cell->getControlId() == controlId)
            cell->flash();
}

void ControllerSurfaceComponent::tickDetectHighlights() {
    for (auto* cell : cells_)
        cell->tickHighlight();
}

bool ControllerSurfaceComponent::isControlPulsingForTest(const juce::String& controlId) const {
    for (auto* cell : cells_)
        if (cell->getControlId() == controlId)
            return cell->hasLiveHighlightForTest();
    return false;
}

void ControllerSurfaceComponent::noteActivity(const juce::String& controlId, synth::midi::RemoteEventKind kind,
                                              float value) {
    for (auto* cell : cells_) {
        if (cell->getControlId() == controlId) {
            cell->noteActivity(kind, value);
            return;
        }
    }
    // Not on the currently shown grid -- the caller doesn't pre-filter (header contract), so this
    // is an ordinary no-op, not an error.
}

float ControllerSurfaceComponent::getCellValueForTest(const juce::String& controlId) const {
    for (auto* cell : cells_)
        if (cell->getControlId() == controlId)
            return cell->getDisplayedValueForTest();
    return -1.0f;
}

const ControllerSurfaceCell* ControllerSurfaceComponent::findCellForTest(const juce::String& controlId) const {
    for (auto* cell : cells_)
        if (cell->getControlId() == controlId)
            return cell;
    return nullptr;
}

ControllerSurfaceCell* ControllerSurfaceComponent::findCellForTest(const juce::String& controlId) {
    return const_cast<ControllerSurfaceCell*>(
        const_cast<const ControllerSurfaceComponent*>(this)->findCellForTest(controlId));
}

void ControllerSurfaceComponent::resized() {
    // Cells are positioned by grid col/row in setControls(), and content_'s own bounds are the
    // fixed pannable extent set in updateTransform() -- neither depends on this component's own
    // size, so there is nothing to lay out here.
}

void ControllerSurfaceComponent::paint(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour background = lf != nullptr ? lf->getTheme().colors.bg1 : juce::Colours::black;
    const juce::Colour textColour = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;

    // Fills behind content_'s own transformed bounds too -- content_ paints its dotted grid only
    // where cells actually exist (Content::paint()), so an empty/zoomed-out margin still needs a
    // background from somewhere.
    g.fillAll(background);

    if (cells_.isEmpty()) {
        g.setColour(textColour.withAlpha(0.5f));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawFittedText("No controller selected", getLocalBounds(), juce::Justification::centred, 2);
    }
}

bool ControllerSurfaceComponent::keyPressed(const juce::KeyPress& key) {
    // Esc clears the selection regardless of how many are selected -- checked first since
    // it is valid even with nothing selected (a no-op, reported as unhandled below).
    if (key == juce::KeyPress::escapeKey) {
        if (selectedIds_.empty())
            return false;
        setSelectionInternal({});
        return true;
    }
    if (selectedIds_.empty())
        return false;
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (onDeleteControlsRequested)
            onDeleteControlsRequested(selectedIds_);
        return true;
    }
    return false;
}

} // namespace synth::ui
