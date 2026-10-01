// Concern: the modulator band's mouse -- Draw and Erase paint or clear a span, Select moves a block or
// drags its edge, a double-click adds a bar -- and the one commit every gesture ends in.
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorBand.h"

#include "AppUndoManager.h"
#include "UI/Timeline/TimelineBeatsPerBar.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr double kFallbackMinBlockBeats = 0.0625; // with snap off, a block is never thinner than this
constexpr double kInsideBeats = 1.0e-3;           // "just inside" an edge, to find the block there
} // namespace

double ModulatorBand::snappedBeat(double rawBeat) const {
    return std::max(0.0, viewState_.snapBeat(rawBeat, beatsPerBar()));
}

double ModulatorBand::snappedBeatAt(int x) const { return snappedBeat(viewState_.xToBeat((double)x)); }

double ModulatorBand::minBlockLength() const {
    const double division = viewState_.divisionBeats(beatsPerBar());
    return division > 0.0 ? division : kFallbackMinBlockBeats;
}

// The nearest edge within the hit zone wins, so two edges a few pixels apart (a thin block) resolve to
// the one under the pointer. An open end has no edge to grab.
std::optional<ModulatorBand::EdgeHit> ModulatorBand::hitEdge(const SectionBlocks& blocks, int x) const {
    std::optional<EdgeHit> best;
    double bestDistance = (double)kEdgeHitPx + 1.0;
    for (int i = 0; i < (int)blocks.size(); ++i) {
        const auto& block = blocks[(size_t)i];
        const double startDistance = std::abs((double)x - viewState_.beatToX(block.start));
        const double endDistance =
            std::isfinite(block.end) ? std::abs((double)x - viewState_.beatToX(block.end)) : bestDistance;
        if (startDistance <= (double)kEdgeHitPx && startDistance < bestDistance) {
            best = EdgeHit{i, true};
            bestDistance = startDistance;
        }
        if (endDistance <= (double)kEdgeHitPx && endDistance < bestDistance) {
            best = EdgeHit{i, false};
            bestDistance = endDistance;
        }
    }
    return best;
}

//==============================================================================
void ModulatorBand::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    drag_ = Drag::None;
    preview_.reset();
    if (!info_.isLfo || doc_ == nullptr || !e.mods.isLeftButtonDown() || e.mods.isPopupMenu())
        return;

    original_ = currentBlocks();
    const int x = e.getPosition().x;
    switch (tool_) {
    case SectionTool::Draw:
        drag_ = Drag::Paint;
        downBeat_ = snappedBeatAt(x);
        break;
    case SectionTool::Erase:
        drag_ = Drag::Erase;
        downBeat_ = snappedBeatAt(x);
        // A band with no sections lane is on everywhere, so erasing out of it leaves "everywhere but here".
        if (!hasSectionsLane())
            original_ = {{0.0, kOpenEnd}};
        break;
    case SectionTool::Select:
        beginSelectDrag(x);
        break;
    }
    repaint();
}

// A press on an edge starts a resize, on a block's middle a move, anywhere else a plain click that
// clears the selection. Either of the first two selects the block at once.
void ModulatorBand::beginSelectDrag(int x) {
    if (const auto edge = hitEdge(original_, x)) {
        drag_ = edge->startEdge ? Drag::ResizeStart : Drag::ResizeEnd;
        dragIndex_ = edge->index;
        selectedStart_ = original_[(size_t)edge->index].start;
        return;
    }
    const double beat = viewState_.xToBeat((double)x);
    const int index = blockIndexAt(original_, beat);
    if (index < 0) {
        selectedStart_.reset();
        return;
    }
    drag_ = Drag::Move;
    dragIndex_ = index;
    grabOffset_ = beat - original_[(size_t)index].start;
    selectedStart_ = original_[(size_t)index].start;
}

void ModulatorBand::mouseDrag(const juce::MouseEvent& e) {
    if (drag_ == Drag::None)
        return;
    updatePreview(e.getPosition().x);
    repaint();
}

// What the drag would write if it ended here. Nothing reaches the doc until mouse-up.
void ModulatorBand::updatePreview(int x) {
    const double beat = snappedBeatAt(x);
    switch (drag_) {
    case Drag::Paint:
        preview_ = paintedSpan(original_, std::min(downBeat_, beat), std::max(downBeat_, beat));
        break;
    case Drag::Erase:
        preview_ = erasedSpan(original_, std::min(downBeat_, beat), std::max(downBeat_, beat));
        break;
    case Drag::ResizeStart:
        preview_ = resizedBlock(original_, dragIndex_, true, beat, minBlockLength());
        selectBeat_ = std::isfinite(original_[(size_t)dragIndex_].end)
                          ? original_[(size_t)dragIndex_].end - kInsideBeats
                          : original_[(size_t)dragIndex_].start;
        break;
    case Drag::ResizeEnd:
        preview_ = resizedBlock(original_, dragIndex_, false, beat, minBlockLength());
        selectBeat_ = original_[(size_t)dragIndex_].start + kInsideBeats;
        break;
    case Drag::Move: {
        const double newStart = snappedBeat(viewState_.xToBeat((double)x) - grabOffset_);
        preview_ = movedBlock(original_, dragIndex_, newStart);
        selectBeat_ = newStart + kInsideBeats;
        break;
    }
    case Drag::None:
        break;
    }
}

void ModulatorBand::mouseUp(const juce::MouseEvent&) { finishDrag(); }

// One gesture, one write: a drag that changed nothing (a click, a drag back to where it began) writes
// nothing at all.
void ModulatorBand::finishDrag() {
    const auto drag = drag_;
    auto result = preview_;
    drag_ = Drag::None;
    preview_.reset();
    repaint();
    if (drag == Drag::None || !result.has_value() || *result == normalisedSections(original_))
        return;
    const bool keepsSelection = drag == Drag::ResizeStart || drag == Drag::ResizeEnd || drag == Drag::Move;
    commit(std::move(*result), keepsSelection ? std::optional<double>(selectBeat_) : std::nullopt);
}

void ModulatorBand::cancelDrag() {
    drag_ = Drag::None;
    preview_.reset();
    repaint();
}

// Everything that changes sections ends here. The band's own state is settled before the write, because the
// doc notification it fires refreshes (and may rebuild) the panel around us: nothing is touched afterwards
// unless the band is still alive. Creating the lane is two doc mutations (the lane, then its points) in one
// undo step, so the selection is protected while it runs and checked once it is done.
void ModulatorBand::commit(SectionBlocks blocks, std::optional<double> selectBeat) {
    const auto track = ownerTrack();
    if (doc_ == nullptr || !track.isValid())
        return;
    blocks = normalisedSections(std::move(blocks));
    selectedStart_.reset();
    if (selectBeat.has_value()) {
        const int index = blockIndexAt(blocks, *selectBeat);
        if (index >= 0)
            selectedStart_ = blocks[(size_t)index].start;
    }
    repaint();

    auto* doc = doc_;
    const auto lfo = info_.sourceUuid;
    auto mutate = [doc, track, lfo, blocks = std::move(blocks)] { applySections(*doc, track, lfo, blocks); };
    juce::Component::SafePointer<ModulatorBand> self(this);
    committing_ = true;
    if (undo_ != nullptr)
        undo_->recordTimelineChange(*doc, mutate);
    else
        mutate();
    if (self != nullptr) {
        self->committing_ = false;
        self->refreshFromDoc();
    }
}

// Empty band space only: a double-click inside a block is left alone, so it can't undo a selection.
void ModulatorBand::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!info_.isLfo || doc_ == nullptr)
        return;
    if (blockIndexAt(currentBlocks(), viewState_.xToBeat((double)e.getPosition().x)) >= 0)
        return;
    addBarAt(snappedBeatAt(e.getPosition().x));
}

void ModulatorBand::addBarAt(double start) {
    commit(paintedSpan(currentBlocks(), start, start + beatsPerBar()), start + kInsideBeats);
}

} // namespace synth::ui
