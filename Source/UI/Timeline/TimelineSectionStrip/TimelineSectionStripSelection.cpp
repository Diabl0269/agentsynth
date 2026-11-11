// TimelineSectionStripSelection.cpp
//
// Which section blocks are selected (a ListSelectionModel over placement ids, the track list's Finder rules) and the
// verbs that act on the whole set: delete with or without the bars, and place again. Each verb is one undo step,
// however many placements it touches. The class is declared in TimelineSectionStrip.h.

#include "UI/Timeline/ListSelectionModel.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"
#include <algorithm>

namespace synth::ui {

namespace {
std::vector<synth::PlacementId> docOrder(const synth::TimelineDoc* doc) {
    std::vector<synth::PlacementId> ids;
    if (doc != nullptr)
        for (const auto& placement : doc->getPlacements())
            ids.push_back(placement.id);
    return ids;
}
} // namespace

// The anchor is the last plain- or Cmd-clicked block, so a Cmd-click that took it out leaves it unselected; the keys
// then act on the first selected block instead.
synth::PlacementId TimelineSectionStrip::getSelectedPlacement() const {
    if (selection_->contains(selection_->anchor()))
        return selection_->anchor();
    const auto ids = getSelectedPlacements();
    return ids.empty() ? synth::PlacementId{} : ids.front();
}

std::vector<synth::PlacementId> TimelineSectionStrip::getSelectedPlacements() const {
    return selection_->ordered(docOrder(doc_));
}

bool TimelineSectionStrip::isPlacementSelected(synth::PlacementId id) const { return selection_->contains(id); }

void TimelineSectionStrip::selectPlacement(synth::PlacementId id) {
    extendEnd_ = {};
    if (selection_->size() == (id.isValid() ? 1 : 0) && (!id.isValid() || selection_->contains(id)))
        return;
    selection_->selectOnly(id);
    repaint();
}

void TimelineSectionStrip::togglePlacementSelected(synth::PlacementId id) {
    extendEnd_ = {};
    selection_->toggle(id);
    repaint();
}

void TimelineSectionStrip::extendPlacementSelectionTo(synth::PlacementId id) {
    selection_->extendTo(id, docOrder(doc_));
    extendEnd_ = id;
    repaint();
}

void TimelineSectionStrip::selectAllPlacements() {
    const auto ids = docOrder(doc_);
    selectPlacements(ids, ids.empty() ? synth::PlacementId{} : ids.front());
}

// The anchor goes last: ListSelectionModel::selectAll makes the last id the anchor.
void TimelineSectionStrip::selectPlacements(const std::vector<synth::PlacementId>& ids, synth::PlacementId anchor) {
    auto ordered = ids;
    const auto it = std::find(ordered.begin(), ordered.end(), anchor);
    if (it != ordered.end())
        std::rotate(it, it + 1, ordered.end());
    selection_->selectAll(ordered);
    extendEnd_ = {};
    repaint();
}

// A verb on `id` reaches the whole set when `id` is one of several selected blocks.
bool TimelineSectionStrip::actsOnSelection(synth::PlacementId id) const {
    return selection_->size() > 1 && selection_->contains(id);
}

// Latest first, so deleting a block's bars never shifts the start of one still to go.
bool TimelineSectionStrip::deleteSelected(bool keepMusic) {
    auto ids = getSelectedPlacements();
    if (doc_ == nullptr || ids.empty())
        return false;
    const double spb = secondsPerBeat();
    bool done = false;
    performEdit([this, &ids, keepMusic, spb, &done] {
        synth::TimelineDoc::SectionEditScope scope(*doc_);
        for (auto it = ids.rbegin(); it != ids.rend(); ++it)
            done |= keepMusic ? doc_->removePlacementKeepMusic(*it) : doc_->deletePlacementAndBars(*it, spb);
    });
    refresh(); // every removed block shrinks away from where it was (trackChangesForMotion)
    return done;
}

// Copies of every selected block, back to back in time order from the end of the last one; the copies become the
// selection.
bool TimelineSectionStrip::placeSelectedAgain() {
    const auto ids = getSelectedPlacements();
    if (doc_ == nullptr || ids.empty())
        return false;
    double at = 0.0;
    for (const auto id : ids)
        if (const auto* placement = doc_->getPlacement(id))
            if (const auto* section = doc_->getSection(placement->section))
                at = std::max(at, placement->startBeat + section->lengthBeats);
    const auto mode = dropMode();
    const double spb = secondsPerBeat();
    std::vector<synth::PlacementId> placed;
    performEdit([this, &ids, at, mode, spb, &placed] { placed = doc_->placeSectionsTogether(ids, at, mode, spb); });
    refresh();
    if (placed.empty())
        return false;
    selectPlacements(placed, placed.back());
    return true;
}

} // namespace synth::ui
