#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <iterator>
#include <set>
#include <vector>

namespace synth::ui {

/**
 * @brief Which timeline tracks are selected, with the list conventions of Finder and Logic.
 *
 * Plain click: one track. Cmd/Ctrl-click: toggles one and moves the anchor to it. Shift-click: the
 * selection is what it was at the last plain or Cmd-click (the "base") plus every track between the
 * anchor and the clicked one, so shift-clicking again somewhere else moves the range's far end rather
 * than piling ranges up. The anchor is the last plain- or Cmd-clicked track.
 *
 * Pure state, free of any Component, so the rules test headlessly. TimelinePanelComponent owns the
 * single instance, and it is ephemeral UI state: it never touches TimelineDoc, undo or persistence
 * (the same rule focusedTrackIndex_ follows). Callers pass the tracks in document order wherever a
 * range or an ordered answer is needed.
 */
class TrackSelectionModel {
public:
    bool isEmpty() const noexcept { return ids_.empty(); }
    int size() const noexcept { return (int)ids_.size(); }
    bool contains(synth::TrackId id) const noexcept { return ids_.find(id) != ids_.end(); }
    synth::TrackId anchor() const noexcept { return anchor_; }

    void clear() noexcept {
        ids_.clear();
        base_.clear();
        anchor_ = {};
    }

    /** A plain click: just this track, and it becomes the anchor. An invalid id clears. */
    void selectOnly(synth::TrackId id) {
        clear();
        if (!id.isValid())
            return;
        ids_.insert(id);
        base_ = ids_;
        anchor_ = id;
    }

    /** Cmd/Ctrl-click: adds the track when absent, removes it when present; the anchor moves to it.
     *  @return the track's selected state after the toggle. */
    bool toggle(synth::TrackId id) {
        if (!id.isValid())
            return false;
        if (ids_.erase(id) == 0)
            ids_.insert(id);
        base_ = ids_;
        anchor_ = id;
        return contains(id);
    }

    /** Shift-click (or Shift+arrow): the base selection plus every track from the anchor to `id`.
     *  With no usable anchor it behaves as a plain click on `id`. */
    void extendTo(synth::TrackId id, const std::vector<synth::TrackId>& order) {
        const auto toIt = std::find(order.begin(), order.end(), id);
        const auto fromIt = std::find(order.begin(), order.end(), anchor_);
        if (toIt == order.end())
            return;
        if (fromIt == order.end()) {
            selectOnly(id);
            return;
        }
        ids_ = base_;
        for (auto it = std::min(fromIt, toIt); it <= std::max(fromIt, toIt); ++it)
            ids_.insert(*it);
    }

    /** Drops tracks that are no longer in `order`; an anchor that went away is forgotten. */
    void retainOnly(const std::vector<synth::TrackId>& order) {
        const auto gone = [&order](synth::TrackId id) {
            return std::find(order.begin(), order.end(), id) == order.end();
        };
        for (auto* set : {&ids_, &base_})
            for (auto it = set->begin(); it != set->end();)
                it = gone(*it) ? set->erase(it) : std::next(it);
        if (gone(anchor_))
            anchor_ = {};
    }

    /** The selected tracks in the order given (document order), whatever order they were clicked in. */
    std::vector<synth::TrackId> ordered(const std::vector<synth::TrackId>& order) const {
        std::vector<synth::TrackId> out;
        for (const auto id : order)
            if (contains(id))
                out.push_back(id);
        return out;
    }

private:
    std::set<synth::TrackId> ids_;
    std::set<synth::TrackId> base_; // ids_ as of the last plain or Cmd-click: what a Shift range adds to
    synth::TrackId anchor_;
};

} // namespace synth::ui
