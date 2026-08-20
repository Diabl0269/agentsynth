#pragma once

#include "../Timeline/TimelineDoc.h"
#include <juce_graphics/juce_graphics.h>
#include <set>
#include <utility>
#include <vector>

namespace synth::ui {

/**
 * @brief The set of notes currently selected inside an open PianoRollComponent.
 *
 * ClipSelectionModel's sibling (Source/UI/ClipSelectionModel.h) — same pure state + set-algebra
 * shape, free of any Component/TimelineDoc dependency, just keyed on synth::NoteId instead of
 * synth::ClipId. PianoRollComponent owns one instance directly (a note selection has no meaning
 * outside an open roll, so unlike ClipSelectionModel/TimelineClipLaneArea there is no separate
 * panel-level owner handing this in by reference).
 *
 * Ids sit in a std::set ordered by NoteId::value (detail::TimelineId already defines operator<
 * that way) so getSelected() always returns ascending-id order regardless of click order — the
 * same reason ClipSelectionModel does it: a batched multi-note mutation (move, delete, velocity
 * scrub) walks notes in a stable order rather than one that depends on click order.
 */
class NoteSelectionModel {
public:
    void clear() noexcept { ids.clear(); }
    bool isEmpty() const noexcept { return ids.empty(); }
    int size() const noexcept { return (int)ids.size(); }
    bool contains(synth::NoteId id) const noexcept { return ids.find(id) != ids.end(); }

    /** Adds an id. An invalid NoteId (value <= 0 — TimelineDoc's "not found" sentinel) is never
     *  selectable.
     *  @return true if the id was newly added (false when already present or invalid). */
    bool add(synth::NoteId id) {
        if (!id.isValid())
            return false;
        return ids.insert(id).second;
    }

    /** @return true if the id was present and removed. */
    bool remove(synth::NoteId id) { return ids.erase(id) > 0; }

    /** Adds the id when absent, removes it when present.
     *  @return the id's selected state AFTER the toggle. */
    bool toggle(synth::NoteId id) {
        if (contains(id)) {
            remove(id);
            return false;
        }
        return add(id);
    }

    /** Replaces the whole selection. Invalid ids are dropped, duplicates collapse. */
    void setSelection(const std::vector<synth::NoteId>& newIds) {
        ids.clear();
        for (auto id : newIds)
            add(id);
    }

    /** Selected ids in ascending id order. */
    std::vector<synth::NoteId> getSelected() const { return {ids.begin(), ids.end()}; }

    /** Drops every selected id that is not in `alive`. Call after any doc mutation that can make
     *  a note id stop existing (delete, undo, the edited clip itself disappearing) so the
     *  selection can never name a freed note.
     *  @return true if anything was dropped. */
    bool retainOnly(const std::vector<synth::NoteId>& alive) {
        std::set<synth::NoteId> aliveSet(alive.begin(), alive.end());
        const auto before = ids.size();
        for (auto it = ids.begin(); it != ids.end();)
            it = (aliveSet.find(*it) == aliveSet.end()) ? ids.erase(it) : std::next(it);
        return ids.size() != before;
    }

private:
    std::set<synth::NoteId> ids;
};

/** Ids of every note rect the marquee touches. Intersection, not containment — same rationale as
 *  ClipSelectionModel.h's clipHitTestMarquee: requiring full enclosure would make a note clipped
 *  at the grid's edge practically unselectable. A degenerate (zero-width or zero-height) marquee
 *  touches nothing. */
inline std::vector<synth::NoteId>
noteHitTestMarquee(juce::Rectangle<int> marquee,
                   const std::vector<std::pair<synth::NoteId, juce::Rectangle<int>>>& noteRects) {
    std::vector<synth::NoteId> hits;
    if (marquee.isEmpty())
        return hits;

    for (const auto& [id, rect] : noteRects) {
        if (!id.isValid())
            continue;
        if (marquee.intersects(rect))
            hits.push_back(id);
    }
    return hits;
}

} // namespace synth::ui
