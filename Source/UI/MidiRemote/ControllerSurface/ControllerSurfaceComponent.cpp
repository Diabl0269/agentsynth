// ControllerSurfaceComponent.cpp -- FRO131 (docs/control/midi-remote-ui.md#surface-centre): builds
// and positions the grid of ControllerSurfaceCells, forwards activity/selection, and turns a
// completed drag into onControlMoved.
//
// MID-GESTURE REBUILD HAZARD (Source/UI/CLAUDE.md's rebuild rule): onControlMoved is fired from
// inside a cell's own mouseUp() call stack (ControllerSurfaceCell::mouseUp -> onDragEnded ->
// the lambda below -> onControlMoved). If THIS component reacted by synchronously calling
// setControls() (which clears cells_, destroying the very cell whose mouseUp is still on the
// stack), the cell would finish its own mouseUp on a freed `this` -- the exact crash class
// GraphEditor::cancelLiveDragGestures() exists to avoid on the canvas side. So this component does
// nothing destructive in response to its own onControlMoved/onDragEnded: it only computes the
// clamped new grid position and invokes the callback, then returns. The caller
// (MidiRemotePanelComponent, one level up) is the one that owns the deferral -- it defers its OWN
// setControls() rebuild via juce::MessageManager::callAsync after receiving onControlMoved, per
// this ticket's brief -- so by the time cells_ is ever rebuilt, the gesture's call stack has long
// since unwound.

#include "ControllerSurfaceComponent.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <memory>

namespace synth::ui {

namespace {
// Per-cell drag accumulator, shared between a cell's onDraggedByCells (fires many times per drag)
// and its onDragEnded (fires once, computes the final clamped position). Lives in a shared_ptr
// captured by both lambdas rather than as component/cell state, since ControllerSurfaceComponent.h
// and ControllerSurfaceCell.h are both locked contracts for this ticket -- see their own header
// comments -- so no new private member can carry it.
struct CellDragState {
    int startCol = 0;
    int startRow = 0;
    int deltaCol = 0;
    int deltaRow = 0;
};
} // namespace

ControllerSurfaceComponent::ControllerSurfaceComponent() {
    // Delete/Backspace must reach THIS component's keyPressed(), not go looking for a focused
    // cell -- cells are mouse-inert display widgets and never take focus themselves.
    setWantsKeyboardFocus(true);
}

ControllerSurfaceComponent::~ControllerSurfaceComponent() = default;

void ControllerSurfaceComponent::setControls(const juce::String& profileId, const std::vector<CellModel>& cells) {
    profileId_ = profileId;
    cells_.clear();

    for (const auto& cellModel : cells) {
        auto* cell = cells_.add(new ControllerSurfaceCell());
        cell->configure(cellModel.control, cellModel.assignmentLabel, cellModel.isWarning, cellModel.isMapped);

        const int x = kCellMargin + cellModel.control.layout.col * (kCellSize + kCellMargin);
        const int y = kCellMargin + cellModel.control.layout.row * (kCellSize + kCellMargin);
        cell->setBounds(x, y, kCellSize, kCellSize);
        addAndMakeVisible(cell);

        const juce::String controlId = cellModel.control.id;

        cell->onSelected = [this, controlId]() {
            setSelectedControlId(controlId);
            if (onSelectControl)
                onSelectControl(controlId);
            // Delete/Backspace must reach keyPressed() below -- grab focus at the moment of
            // selection, per the header's own doc comment on onDeleteControlRequested.
            grabKeyboardFocus();
        };

        auto dragState = std::make_shared<CellDragState>();
        dragState->startCol = cellModel.control.layout.col;
        dragState->startRow = cellModel.control.layout.row;

        cell->onDraggedByCells = [dragState](int dCols, int dRows) {
            dragState->deltaCol = dCols;
            dragState->deltaRow = dRows;
        };

        cell->onDragEnded = [this, controlId, dragState]() {
            const int newCol = juce::jmax(0, dragState->startCol + dragState->deltaCol);
            const int newRow = juce::jmax(0, dragState->startRow + dragState->deltaRow);
            dragState->deltaCol = 0;
            dragState->deltaRow = 0;
            // See this file's top-of-file comment: fire and return, never rebuild cells_ here.
            if (onControlMoved)
                onControlMoved(controlId, newCol, newRow);
        };

        cell->setSelected(controlId == selectedControlId_);
    }
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

void ControllerSurfaceComponent::setSelectedControlId(const juce::String& controlId) {
    selectedControlId_ = controlId;
    for (auto* cell : cells_)
        cell->setSelected(cell->getControlId() == controlId);
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
    if (selectedControlId_.isEmpty())
        return false;
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (onDeleteControlRequested)
            onDeleteControlRequested(selectedControlId_);
        return true;
    }
    return false;
}

} // namespace synth::ui
