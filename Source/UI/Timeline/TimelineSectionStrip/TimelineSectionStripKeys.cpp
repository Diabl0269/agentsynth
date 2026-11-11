// TimelineSectionStripKeys.cpp
//
// The Sections strip's keyboard. The strip takes focus (a click, or Tab) and the selected block is the focused one:
// Left/Right/Home/End walk the blocks, Return or the Rename key renames, Cmd+D places the section again after it,
// Option+Left/Right (the clip move chord) moves it one section earlier or later, V opens its variation menu, Delete or
// Backspace deletes it and its bars, and the open-menu key (Shift+F10) opens its menu through
// KeyboardContextMenuProvider. Left/Right, Option+Left/Right and Cmd+D are the same chords as the clip and track-row
// verbs, so they resolve through those actions rather than a second binding that would read as a conflict. Esc cancels
// a drag in flight.

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

namespace synth::ui {

namespace {
juce::KeyPress plain(int code) { return juce::KeyPress(code, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress withAlt(int code) { return juce::KeyPress(code, juce::ModifierKeys::altModifier, 0); }
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
    if (!selected_.isValid()) {
        selectPlacement(step > 0 ? placements.front().id : placements.back().id);
        return;
    }
    for (std::size_t i = 0; i < placements.size(); ++i) {
        if (placements[i].id != selected_)
            continue;
        const auto next = (long)i + step;
        if (next >= 0 && next < (long)placements.size())
            selectPlacement(placements[(std::size_t)next].id);
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
    if (doc_ == nullptr || !selected_.isValid())
        return false;
    const auto& placements = doc_->getPlacements();
    for (std::size_t i = 0; i < placements.size(); ++i) {
        if (placements[i].id != selected_)
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
        const auto id = selected_;
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

bool TimelineSectionStrip::showContextMenuForKeyboardFocus() {
    if (doc_ == nullptr || !selected_.isValid() || doc_->getPlacement(selected_) == nullptr)
        return false;
    openSectionContextMenu(selected_);
    return true;
}

bool TimelineSectionStrip::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey)
        return cancelGesture();
    if (doc_ == nullptr || makeDragging_ || blockDragging_ || renameEditor_ != nullptr)
        return false;

    if (matchesKey("timelineClipMoveEarlier", withAlt(juce::KeyPress::leftKey), key)) {
        if (selected_.isValid())
            moveSelectedBlock(-1);
        return selected_.isValid();
    }
    if (matchesKey("timelineClipMoveLater", withAlt(juce::KeyPress::rightKey), key)) {
        if (selected_.isValid())
            moveSelectedBlock(1);
        return selected_.isValid();
    }
    if (matchesKey("timelineClipPrevious", plain(juce::KeyPress::leftKey), key)) {
        selectNeighbour(-1);
        return true;
    }
    if (matchesKey("timelineClipNext", plain(juce::KeyPress::rightKey), key)) {
        selectNeighbour(1);
        return true;
    }
    if (key == plain(juce::KeyPress::homeKey) || key == plain(juce::KeyPress::endKey)) {
        selectEnd(key == plain(juce::KeyPress::endKey));
        return true;
    }
    if (!selected_.isValid())
        return false; // an empty selection has no verb: Return makes nothing (a block needs a range to come from)

    const bool renameKey = key == plain(juce::KeyPress::returnKey) || key == plain(juce::KeyPress::F2Key) ||
                           (shortcuts_ != nullptr &&
                            (ShortcutManager::keyPressMatches(shortcuts_->getBinding("renameSelectedModule"), key) ||
                             ShortcutManager::keyPressMatches(shortcuts_->getBinding("renameSelectedModuleAlt"), key)));
    if (renameKey) {
        beginRenameSection(selected_);
        return renameEditor_ != nullptr;
    }
    if (key == plain('v')) {
        openVariationMenu(selected_);
        return true;
    }
    if (matchesKey("timelineDuplicateFocusedTrack", juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0), key)) {
        applySectionMenuChoice(selected_, SectionMenuChoice::PlaceAgainAfter);
        return true;
    }
    if (key == plain(juce::KeyPress::deleteKey) || key == plain(juce::KeyPress::backspaceKey)) {
        applySectionMenuChoice(selected_, SectionMenuChoice::DeleteWithBars);
        return true;
    }
    return false;
}

} // namespace synth::ui
