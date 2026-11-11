// TimelineSectionStripKeys.cpp
//
// The Sections strip's keyboard. The strip takes focus (a click, or Tab) and the selected block is the focused one:
// Left/Right/Home/End walk the blocks (selecting just the one they land on), Shift+Left/Right extend the selection a
// block at a time, Cmd+A selects every block, Return or the Rename key renames, V opens the variation menu, and the
// open-menu key (Shift+F10) opens the block menu through KeyboardContextMenuProvider (those four act on the focused
// block). Over the whole selection: Cmd+D places every selected block again after the last one, Option+Left/Right (the
// clip move chord) moves the group past the neighbouring unselected block, and Delete or Backspace deletes them and
// their bars, each one undo step. Left/Right, Option+Left/Right, Cmd+D and Cmd+A are the same chords as the clip and
// track-row verbs, so they resolve through those actions rather than a second binding that would read as a conflict.
// Esc cancels a drag in flight.

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

namespace synth::ui {

namespace {
juce::KeyPress plain(int code) { return juce::KeyPress(code, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress withAlt(int code) { return juce::KeyPress(code, juce::ModifierKeys::altModifier, 0); }
juce::KeyPress withShift(int code) { return juce::KeyPress(code, juce::ModifierKeys::shiftModifier, 0); }
} // namespace

bool TimelineSectionStrip::matchesKey(const juce::String& actionId, const juce::KeyPress& fallback,
                                      const juce::KeyPress& key) const {
    if (shortcuts_ == nullptr)
        return key == fallback;
    return ShortcutManager::keyPressMatches(shortcuts_->getBinding(actionId), key);
}

// Moves the selection `step` placements along the doc order (-1 earlier, 1 later); with nothing selected, Right lands
// on the first and Left on the last.
void TimelineSectionStrip::selectNeighbour(int step) {
    if (doc_ == nullptr || doc_->getPlacements().empty())
        return;
    const auto& placements = doc_->getPlacements();
    const auto selected = getSelectedPlacement();
    if (!selected.isValid()) {
        selectPlacement(step > 0 ? placements.front().id : placements.back().id);
        return;
    }
    for (std::size_t i = 0; i < placements.size(); ++i) {
        if (placements[i].id != selected)
            continue;
        const auto next = (long)i + step;
        if (next >= 0 && next < (long)placements.size())
            selectPlacement(placements[(std::size_t)next].id);
        return;
    }
}

// Shift+Left/Right: the far end of the run moves one block and the selection is the run from the anchor to it.
void TimelineSectionStrip::extendSelection(int step) {
    if (doc_ == nullptr || doc_->getPlacements().empty())
        return;
    const auto& placements = doc_->getPlacements();
    const auto from = extendEnd_.isValid() && isPlacementSelected(extendEnd_) ? extendEnd_ : getSelectedPlacement();
    if (!from.isValid()) {
        selectPlacement(step > 0 ? placements.front().id : placements.back().id);
        return;
    }
    for (std::size_t i = 0; i < placements.size(); ++i) {
        if (placements[i].id != from)
            continue;
        const auto next = (long)i + step;
        if (next >= 0 && next < (long)placements.size())
            extendPlacementSelectionTo(placements[(std::size_t)next].id);
        return;
    }
}

void TimelineSectionStrip::selectEnd(bool last) {
    if (doc_ == nullptr || doc_->getPlacements().empty())
        return;
    selectPlacement(last ? doc_->getPlacements().back().id : doc_->getPlacements().front().id);
}

// One section earlier lands on the previous block's start; one later lands just after the next block's end (the two
// swap places, any gap between them staying where it was). Always make-room, so no music is covered. False at either
// end.
bool TimelineSectionStrip::moveSelectedBlock(int step) {
    const auto selected = getSelectedPlacement();
    if (doc_ == nullptr || !selected.isValid())
        return false;
    if (getSelectedPlacements().size() > 1)
        return moveSelectedGroup(step);
    const auto& placements = doc_->getPlacements();
    for (std::size_t i = 0; i < placements.size(); ++i) {
        if (placements[i].id != selected)
            continue;
        const auto neighbour = (long)i + step;
        if (neighbour < 0 || neighbour >= (long)placements.size())
            return false;
        const auto& other = placements[(std::size_t)neighbour];
        const auto* otherSection = doc_->getSection(other.section);
        if (otherSection == nullptr)
            return false;
        // The target is the block's start after the move, counted with its own bars closed up: one later lands where
        // the next block ends once it has moved up into the gap.
        const auto* ownSection = doc_->getSection(placements[i].section);
        if (ownSection == nullptr)
            return false;
        const double length = ownSection->lengthBeats;
        const double target = step < 0 ? other.startBeat : other.startBeat + otherSection->lengthBeats - length;
        const auto id = selected;
        const double spb = secondsPerBeat();
        bool moved = false;
        performEdit([this, id, target, spb, &moved] {
            moved = doc_->movePlacement(id, target, synth::SectionDropMode::MakeRoom, spb);
        });
        refresh();
        return moved;
    }
    return false;
}

// The selected group moves past the neighbouring unselected block: later, past the first one after the last selected
// block; earlier, before the last one ahead of the first. The group lands back to back in time order, counted with its
// own bars closed up as a drag's landing is (every member is before a later neighbour, after an earlier one). False at
// either end.
bool TimelineSectionStrip::moveSelectedGroup(int step) {
    const auto ids = getSelectedPlacements();
    const auto& placements = doc_->getPlacements();
    std::size_t first = placements.size();
    std::size_t last = 0;
    double length = 0.0;
    for (std::size_t i = 0; i < placements.size(); ++i)
        if (isPlacementSelected(placements[i].id)) {
            first = std::min(first, i);
            last = i;
            if (const auto* section = doc_->getSection(placements[i].section))
                length += section->lengthBeats;
        }
    if (first == placements.size())
        return false;
    const synth::SectionPlacement* neighbour = nullptr;
    if (step < 0) {
        for (std::size_t i = first; i-- > 0;)
            if (!isPlacementSelected(placements[i].id)) {
                neighbour = &placements[i];
                break;
            }
    } else {
        for (std::size_t i = last + 1; i < placements.size() && neighbour == nullptr; ++i)
            if (!isPlacementSelected(placements[i].id))
                neighbour = &placements[i];
    }
    const auto* section = neighbour != nullptr ? doc_->getSection(neighbour->section) : nullptr;
    if (section == nullptr)
        return false;
    const double target = step < 0 ? neighbour->startBeat : neighbour->startBeat + section->lengthBeats - length;
    const double spb = secondsPerBeat();
    bool moved = false;
    performEdit([this, &ids, target, spb, &moved] {
        moved = doc_->movePlacementsTogether(ids, target, synth::SectionDropMode::MakeRoom, spb);
    });
    refresh();
    return moved;
}

bool TimelineSectionStrip::showContextMenuForKeyboardFocus() {
    const auto selected = getSelectedPlacement();
    if (doc_ == nullptr || !selected.isValid() || doc_->getPlacement(selected) == nullptr)
        return false;
    openSectionContextMenu(selected);
    return true;
}

bool TimelineSectionStrip::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey)
        return cancelGesture();
    if (doc_ == nullptr || makeDragging_ || blockDragging_ || renameEditor_ != nullptr)
        return false;
    const auto selected = getSelectedPlacement();

    if (matchesKey("timelineClipMoveEarlier", withAlt(juce::KeyPress::leftKey), key)) {
        if (selected.isValid())
            moveSelectedBlock(-1);
        return selected.isValid();
    }
    if (matchesKey("timelineClipMoveLater", withAlt(juce::KeyPress::rightKey), key)) {
        if (selected.isValid())
            moveSelectedBlock(1);
        return selected.isValid();
    }
    if (matchesKey("timelineClipPrevious", plain(juce::KeyPress::leftKey), key)) {
        selectNeighbour(-1);
        return true;
    }
    if (matchesKey("timelineClipNext", plain(juce::KeyPress::rightKey), key)) {
        selectNeighbour(1);
        return true;
    }
    if (key == withShift(juce::KeyPress::leftKey) || key == withShift(juce::KeyPress::rightKey)) {
        extendSelection(key == withShift(juce::KeyPress::rightKey) ? 1 : -1);
        return true;
    }
    if (matchesKey("timelineSelectAllTracks", juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0), key)) {
        selectAllPlacements();
        return true;
    }
    if (key == plain(juce::KeyPress::homeKey) || key == plain(juce::KeyPress::endKey)) {
        selectEnd(key == plain(juce::KeyPress::endKey));
        return true;
    }
    if (!selected.isValid())
        return false; // an empty selection has no verb: Return makes nothing (a block needs a range to come from)

    const bool renameKey = key == plain(juce::KeyPress::returnKey) || key == plain(juce::KeyPress::F2Key) ||
                           (shortcuts_ != nullptr &&
                            (ShortcutManager::keyPressMatches(shortcuts_->getBinding("renameSelectedModule"), key) ||
                             ShortcutManager::keyPressMatches(shortcuts_->getBinding("renameSelectedModuleAlt"), key)));
    if (renameKey) {
        beginRenameSection(selected);
        return renameEditor_ != nullptr;
    }
    if (key == plain('v')) {
        openVariationMenu(selected);
        return true;
    }
    if (matchesKey("timelineDuplicateFocusedTrack", juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0), key)) {
        applySectionMenuChoice(selected, SectionMenuChoice::PlaceAgainAfter);
        return true;
    }
    if (key == plain(juce::KeyPress::deleteKey) || key == plain(juce::KeyPress::backspaceKey)) {
        applySectionMenuChoice(selected, SectionMenuChoice::DeleteWithBars);
        return true;
    }
    return false;
}

} // namespace synth::ui
