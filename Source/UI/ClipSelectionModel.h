#pragma once

#include "../Timeline/TimelineDoc.h"
#include <juce_graphics/juce_graphics.h>
#include <set>
#include <utility>
#include <vector>

namespace synth::ui {

/**
 * @brief The set of clips the user currently has selected in the timeline's clip lanes.
 *
 * Same shape as SelectionModel (Source/UI/SelectionModel.h) — pure state + set algebra, free of
 * any Component or TimelineDoc dependency so the multi-select rules are testable headlessly — just
 * keyed on synth::ClipId instead of a graph NodeID. TimelineClipLaneArea owns none of this: the
 * TimelinePanelComponent owns one instance and hands it a reference (same relationship
 * TimelinePanelComponent has with its TimelineViewState), so painting and hit-testing agree with
 * whatever else reads the selection.
 *
 * Ids sit in a std::set ordered by ClipId::value — detail::TimelineId already defines operator<
 * that way, so no separate comparator type is needed — meaning getSelected() always returns them
 * in ascending id order regardless of click order. That matters here for the same reason it
 * matters for SelectionModel's snippet extraction: a batched multi-clip mutation (move, delete)
 * walks clips in a stable order rather than one that depends on the order the user clicked them.
 */
class ClipSelectionModel {
public:
    void clear() noexcept { ids.clear(); }
    bool isEmpty() const noexcept { return ids.empty(); }
    int size() const noexcept { return (int)ids.size(); }
    bool contains(synth::ClipId id) const noexcept { return ids.find(id) != ids.end(); }

    /** Adds an id. An invalid ClipId (value <= 0 — TimelineDoc's "not found" sentinel) is never
     *  selectable.
     *  @return true if the id was newly added (false when already present or invalid). */
    bool add(synth::ClipId id) {
        if (!id.isValid())
            return false;
        return ids.insert(id).second;
    }

    /** @return true if the id was present and removed. */
    bool remove(synth::ClipId id) { return ids.erase(id) > 0; }

    /** Adds the id when absent, removes it when present.
     *  @return the id's selected state AFTER the toggle. */
    bool toggle(synth::ClipId id) {
        if (contains(id)) {
            remove(id);
            return false;
        }
        return add(id);
    }

    /** Replaces the whole selection. Invalid ids are dropped, duplicates collapse. */
    void setSelection(const std::vector<synth::ClipId>& newIds) {
        ids.clear();
        for (auto id : newIds)
            add(id);
    }

    /** Selected ids in ascending id order. */
    std::vector<synth::ClipId> getSelected() const { return {ids.begin(), ids.end()}; }

    /** Drops every selected id that is not in `alive`. Call after any doc mutation that can make a
     *  clip id stop existing (delete, split replacing the right half's id, undo, a track/preset
     *  reload) so the selection can never name a freed clip.
     *  @return true if anything was dropped. */
    bool retainOnly(const std::vector<synth::ClipId>& alive) {
        std::set<synth::ClipId> aliveSet(alive.begin(), alive.end());
        const auto before = ids.size();
        for (auto it = ids.begin(); it != ids.end();)
            it = (aliveSet.find(*it) == aliveSet.end()) ? ids.erase(it) : std::next(it);
        return ids.size() != before;
    }

private:
    std::set<synth::ClipId> ids;
};

/** Ids of every clip rect the marquee touches.
 *
 *  Intersection, not containment — same rationale as SelectionModel.h's hitTestMarquee: requiring
 *  full enclosure would make a clip clipped at the lane area's edge (or one that is simply wider
 *  than the visible marquee) practically unselectable. A degenerate (zero-width or zero-height)
 *  marquee touches nothing, so a plain Shift-click with no drag clears rather than selecting
 *  whatever clip sits under the cursor. */
inline std::vector<synth::ClipId>
clipHitTestMarquee(juce::Rectangle<int> marquee,
                   const std::vector<std::pair<synth::ClipId, juce::Rectangle<int>>>& clipRects) {
    std::vector<synth::ClipId> hits;
    if (marquee.isEmpty())
        return hits;

    for (const auto& [id, rect] : clipRects) {
        if (!id.isValid())
            continue;
        if (marquee.intersects(rect))
            hits.push_back(id);
    }
    return hits;
}

} // namespace synth::ui
