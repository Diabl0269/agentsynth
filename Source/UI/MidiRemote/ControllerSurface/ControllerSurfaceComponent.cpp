// ControllerSurfaceComponent.cpp -- FRO131 (docs/control/midi-remote-ui.md#surface-centre): builds
// and positions the grid of ControllerSurfaceCells, forwards activity, and owns the grid-level
// paint/keyboard plumbing shared by every FRO270 concern. Selection (click/shift/cmd) lives in
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
// setControls() rebuild via juce::MessageManager::callAsync after receiving onControlsMoved, per
// this ticket's brief -- so by the time cells_ is ever rebuilt, the gesture's call stack has long
// since unwound.

#include "ControllerSurfaceComponent.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <algorithm>

namespace synth::ui {

ControllerSurfaceComponent::ControllerSurfaceComponent() {
    // Delete/Backspace/Esc must reach THIS component's keyPressed(), not go looking for a focused
    // cell -- cells are mouse-inert display widgets and never take focus themselves.
    setWantsKeyboardFocus(true);
}

ControllerSurfaceComponent::~ControllerSurfaceComponent() = default;

// FRO270: switching to a different controller drops any control selection (matches the pre-FRO270
// behaviour of the panel's own selectedControlId_.clear()); rebuilding the SAME profile (a live
// refresh -- Detect, a move, a delete, an undo/redo) instead prunes the existing selection down to
// whatever ids still exist, so the caller doesn't have to re-apply it after every mutation.
void ControllerSurfaceComponent::setControls(const juce::String& profileId, const std::vector<CellModel>& cells) {
    if (profileId != profileId_)
        selectedIds_.clear();
    profileId_ = profileId;
    cells_.clear();

    for (const auto& cellModel : cells) {
        auto* cell = cells_.add(new ControllerSurfaceCell());
        cell->configure(cellModel.control, cellModel.assignmentLabel, cellModel.isWarning, cellModel.isMapped,
                        cellModel.initialValue);

        const int x = kCellMargin + cellModel.control.layout.col * (kCellSize + kCellMargin);
        const int y = kCellMargin + cellModel.control.layout.row * (kCellSize + kCellMargin);
        cell->setBounds(x, y, kCellSize, kCellSize);
        addAndMakeVisible(cell);

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

void ControllerSurfaceComponent::resized() {
    // Cells are positioned by grid col/row in setControls(), not by this component's own size --
    // nothing to lay out here.
}

void ControllerSurfaceComponent::paint(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour background = lf != nullptr ? lf->getTheme().colors.bg1 : juce::Colours::black;
    const juce::Colour gridLine = lf != nullptr ? lf->getTheme().colors.border : juce::Colours::darkgrey;
    const juce::Colour textColour = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;

    g.fillAll(background);

    if (cells_.isEmpty()) {
        g.setColour(textColour.withAlpha(0.5f));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawFittedText("No controller selected", getLocalBounds(), juce::Justification::centred, 2);
        return;
    }

    // A simple dotted grid so an empty vs. populated surface reads differently in a screenshot,
    // per this file's brief -- nothing fancier is required.
    g.setColour(gridLine.withAlpha(0.3f));
    for (int x = kCellMargin; x < getWidth(); x += (kCellSize + kCellMargin))
        for (int y = kCellMargin; y < getHeight(); y += (kCellSize + kCellMargin))
            g.fillRect(x, y, 1, 1);
}

bool ControllerSurfaceComponent::keyPressed(const juce::KeyPress& key) {
    // FRO270: Esc clears the selection regardless of how many are selected -- checked first since
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
