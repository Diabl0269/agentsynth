// DrumMachineComponent: editing. Every user edit is ONE undo step through AppUndoManager::recordTimelineChange (or a
// direct call when there is no undo manager), and goes through TimelineDoc's mutators only.

#include "DrumMachineComponent.h"

#include "AppUndoManager.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

void DrumMachineComponent::record(const std::function<void()>& mutation) {
    if (doc_ == nullptr)
        return;
    if (undoManager_ != nullptr)
        undoManager_->recordTimelineChange(*doc_, mutation);
    else
        mutation();
}

// Rebuilds the cell index from the clip. Cells that appeared or vanished since the last build grow in or shrink out
// (beginHitMotion), unless `animate` is false or the grid's resolution just changed (every cell would move).
void DrumMachineComponent::rebuildCells(bool animate) {
    auto previous = std::move(cells_);
    cells_.clear();
    const auto* c = clip();
    const double stepLen = getStepBeats();
    if (c != nullptr)
        cells_ = drumgrid::buildCellIndex(*c, stepLen);
    const bool sameGrid = std::abs(cellsStepBeats_ - stepLen) < 1.0e-12;
    cellsStepBeats_ = stepLen;
    // A new grid resolution moves every cell, so nothing shrinks out; an idea that changed it still grows its hits in.
    const bool stagger = staggerNext_ && !prefersReducedMotion();
    if (!animate || (!sameGrid && !staggerNext_)) {
        hitMotions_.clear();
        return;
    }
    for (const auto& [key, hits] : cells_)
        if (previous.count(key) == 0 || !sameGrid)
            beginHitMotion(key, true, hits.velocity,
                           stagger ? std::min(kMaxStaggerMs, kStaggerMsPerColumn * static_cast<double>(key % 1000000))
                                   : 0.0);
    if (!sameGrid)
        return;
    for (const auto& [key, hits] : previous)
        if (cells_.count(key) == 0)
            beginHitMotion(key, false, hits.velocity);
}

void DrumMachineComponent::audition(int pitch, int velocity) {
    if (!onAuditionNote)
        return;
    const float v = static_cast<float>(velocity) / 127.0f;
    onAuditionNote(pitch, v, true);
    onAuditionNote(pitch, v, false);
}

// A click on an empty cell: one note at the cell's start, one step long.
void DrumMachineComponent::addHit(int pitch, int step, int velocity) {
    if (clip() == nullptr || step < 0)
        return;
    synth::MidiNote note;
    note.startBeat = static_cast<double>(step) * getStepBeats();
    note.lengthBeats = getStepBeats();
    note.pitch = pitch;
    note.velocity = juce::jlimit(1, 127, velocity);
    const auto id = clipId_;
    record([this, id, note] { doc_->addNote(id, note); });
    audition(pitch, note.velocity);
    refreshFromDoc();
}

void DrumMachineComponent::removeHits(int pitch, int step) {
    const auto it = cells_.find(drumgrid::cellKey(pitch, step));
    if (it == cells_.end())
        return;
    const auto ids = it->second.ids;
    record([this, ids] {
        for (const auto id : ids)
            doc_->removeNote(id);
    });
    refreshFromDoc();
}

void DrumMachineComponent::setCellVelocity(int pitch, int step, int velocity) {
    const auto it = cells_.find(drumgrid::cellKey(pitch, step));
    if (it == cells_.end())
        return;
    const auto ids = it->second.ids;
    const int clamped = juce::jlimit(1, 127, velocity);
    record([this, ids, clamped] {
        for (const auto id : ids)
            doc_->setNoteVelocity(id, clamped);
    });
    refreshFromDoc();
}

// Accent is velocity >= drumgrid::kAccentThreshold: a hit with one toggles down to the normal loudness, any other up to
// the accent.
void DrumMachineComponent::toggleAccent(int pitch, int step) {
    const auto it = cells_.find(drumgrid::cellKey(pitch, step));
    if (it == cells_.end())
        return;
    setCellVelocity(pitch, step, it->second.accent() ? drumgrid::kNewHitVelocity : drumgrid::kAccentVelocity);
}

void DrumMachineComponent::toggleHit(int pitch, int step, bool accent) {
    const bool occupied = cells_.count(drumgrid::cellKey(pitch, step)) > 0;
    if (!occupied)
        addHit(pitch, step, accent ? drumgrid::kAccentVelocity : drumgrid::kNewHitVelocity);
    else if (accent)
        toggleAccent(pitch, step);
    else
        removeHits(pitch, step);
}

void DrumMachineComponent::setStepsPerBar(int stepsPerBarValue) {
    if (clip() == nullptr || !synth::Clip::isValidStepsPerBar(stepsPerBarValue))
        return;
    const auto id = clipId_;
    record([this, id, stepsPerBarValue] { doc_->setClipStepsPerBar(id, stepsPerBarValue); });
    refreshFromDoc();
}

void DrumMachineComponent::setMuted(int pitch, bool muted) {
    if (clip() == nullptr)
        return;
    const auto id = clipId_;
    record([this, id, pitch, muted] { doc_->setClipPitchMuted(id, pitch, muted); });
    refreshFromDoc();
}

void DrumMachineComponent::setSoloed(int pitch, bool soloed) {
    if (clip() == nullptr)
        return;
    const auto id = clipId_;
    record([this, id, pitch, soloed] { doc_->setClipPitchSoloed(id, pitch, soloed); });
    refreshFromDoc();
}

} // namespace synth::ui
