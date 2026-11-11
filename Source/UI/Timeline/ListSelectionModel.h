#pragma once

#include <algorithm>
#include <iterator>
#include <set>
#include <vector>

namespace synth::ui {

/**
 * @brief Which items of an ordered list are selected, with the list conventions of Finder and Logic.
 *
 * Plain click: one item. Cmd/Ctrl-click: toggles one and moves the anchor to it. Shift-click: the selection is what
 * it was at the last plain or Cmd-click (the "base") plus every item between the anchor and the clicked one, so
 * shift-clicking again somewhere else moves the range's far end rather than piling ranges up. The anchor is the last
 * plain- or Cmd-clicked item.
 *
 * Pure state, free of any Component, so the rules test headlessly. It is ephemeral UI state: it never touches
 * TimelineDoc, undo or persistence. Callers pass the items in document order wherever a range or an ordered answer is
 * needed. `Id` needs isValid(), operator< and operator==, and a default-constructed one is invalid (the timeline ids).
 */
template <typename Id>
class ListSelectionModel {
public:
    bool isEmpty() const noexcept { return ids_.empty(); }
    int size() const noexcept { return (int)ids_.size(); }
    bool contains(Id id) const noexcept { return ids_.find(id) != ids_.end(); }
    Id anchor() const noexcept { return anchor_; }

    void clear() noexcept {
        ids_.clear();
        base_.clear();
        anchor_ = {};
    }

    /** A plain click: just this item, and it becomes the anchor. An invalid id clears. */
    void selectOnly(Id id) {
        clear();
        if (!id.isValid())
            return;
        ids_.insert(id);
        base_ = ids_;
        anchor_ = id;
    }

    /** Exactly these items (a duplicate's copies, a paste); the last one is the anchor. */
    void selectAll(const std::vector<Id>& ids) {
        clear();
        for (const auto id : ids)
            if (id.isValid())
                ids_.insert(id);
        base_ = ids_;
        if (!ids.empty())
            anchor_ = ids.back();
    }

    /** Cmd/Ctrl-click: adds the item when absent, removes it when present; the anchor moves to it.
     *  @return the item's selected state after the toggle. */
    bool toggle(Id id) {
        if (!id.isValid())
            return false;
        if (ids_.erase(id) == 0)
            ids_.insert(id);
        base_ = ids_;
        anchor_ = id;
        return contains(id);
    }

    /** Shift-click (or Shift+arrow): the base selection plus every item from the anchor to `id`.
     *  With no usable anchor it behaves as a plain click on `id`. */
    void extendTo(Id id, const std::vector<Id>& order) {
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

    /** Drops items that are no longer in `order`; an anchor that went away is forgotten. */
    void retainOnly(const std::vector<Id>& order) {
        const auto gone = [&order](Id id) { return std::find(order.begin(), order.end(), id) == order.end(); };
        for (auto* set : {&ids_, &base_})
            for (auto it = set->begin(); it != set->end();)
                it = gone(*it) ? set->erase(it) : std::next(it);
        if (gone(anchor_))
            anchor_ = {};
    }

    /** The selected items in the order given (document order), whatever order they were clicked in. */
    std::vector<Id> ordered(const std::vector<Id>& order) const {
        std::vector<Id> out;
        for (const auto id : order)
            if (contains(id))
                out.push_back(id);
        return out;
    }

private:
    std::set<Id> ids_;
    std::set<Id> base_; // ids_ as of the last plain or Cmd-click: what a Shift range adds to
    Id anchor_;
};

} // namespace synth::ui
