#include "PointSelectionModel.h"
#include <algorithm>

namespace synth::ui {

namespace {
template <typename Key>
std::vector<Key> sortedUnique(std::vector<Key> keys) {
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

template <typename Key>
bool has(const std::vector<Key>& sorted, Key key) {
    return std::binary_search(sorted.begin(), sorted.end(), key);
}
} // namespace

// Keys compare exactly (a lane's beats included): a dot is the same dot only when its key is identical, the rule the
// set of beats a lane used before kept too.
template <typename Key>
bool PointSelectionModel<Key>::contains(Key key) const {
    return has(selected_, key);
}

template <typename Key>
bool PointSelectionModel<Key>::clear() {
    if (selected_.empty())
        return false;
    selected_.clear();
    return true;
}

template <typename Key>
bool PointSelectionModel<Key>::setSelected(std::vector<Key> keys) {
    keys = sortedUnique(std::move(keys));
    if (keys == selected_)
        return false;
    selected_ = std::move(keys);
    return true;
}

template <typename Key>
bool PointSelectionModel<Key>::setCursor(std::optional<Key> key) {
    if (cursor_ == key)
        return false;
    cursor_ = key;
    return true;
}

template <typename Key>
bool PointSelectionModel<Key>::assign(std::vector<Key> keys, std::optional<Key> cursor, std::optional<Key> anchor) {
    keys = sortedUnique(std::move(keys));
    if (keys == selected_ && cursor == cursor_ && anchor == anchor_)
        return false;
    selected_ = std::move(keys);
    cursor_ = cursor;
    anchor_ = anchor;
    return true;
}

template <typename Key>
bool PointSelectionModel<Key>::add(Key key) {
    const auto at = std::lower_bound(selected_.begin(), selected_.end(), key);
    if (at != selected_.end() && *at == key)
        return false;
    selected_.insert(at, key);
    return true;
}

template <typename Key>
bool PointSelectionModel<Key>::remove(Key key) {
    const auto at = std::lower_bound(selected_.begin(), selected_.end(), key);
    if (at == selected_.end() || *at != key)
        return false;
    selected_.erase(at);
    return true;
}

template <typename Key>
bool PointSelectionModel<Key>::retainOnly(const std::vector<Key>& alive) {
    const auto aliveSorted = sortedUnique(alive);
    const auto before = selected_.size();
    selected_.erase(
        std::remove_if(selected_.begin(), selected_.end(), [&aliveSorted](Key k) { return !has(aliveSorted, k); }),
        selected_.end());
    bool changed = selected_.size() != before;
    if (cursor_.has_value() && !has(aliveSorted, *cursor_)) {
        cursor_.reset();
        changed = true;
    }
    if (anchor_.has_value() && !has(aliveSorted, *anchor_))
        anchor_.reset();
    return changed;
}

template <typename Key>
void PointSelectionModel<Key>::rekey(const std::function<Key(Key)>& map) {
    for (auto& key : selected_)
        key = map(key);
    std::sort(selected_.begin(), selected_.end());
    if (cursor_.has_value())
        cursor_ = map(*cursor_);
    if (anchor_.has_value())
        anchor_ = map(*anchor_);
}

template <typename Key>
std::optional<Key> PointSelectionModel<Key>::neighbour(Key from, int direction,
                                                       const std::vector<Key>& pickable) const {
    if (direction > 0) {
        const auto next = std::upper_bound(pickable.begin(), pickable.end(), from);
        return next != pickable.end() ? std::optional<Key>(*next) : std::nullopt;
    }
    const auto at = std::lower_bound(pickable.begin(), pickable.end(), from);
    return at != pickable.begin() ? std::optional<Key>(*(at - 1)) : std::nullopt;
}

// The nearest-end rule also restarts from an end when the cursor stands on no pickable dot (a dot that went), so a
// press never walks from a place the user cannot see.
template <typename Key>
std::optional<Key> PointSelectionModel<Key>::step(int direction, const std::vector<Key>& pickable,
                                                  const PointSelectionRules& rules) {
    if (pickable.empty())
        return std::nullopt;
    const bool nearestEnd = rules.firstStep == PointSelectionRules::FirstStep::nearestEnd;
    Key target{};
    if (!cursor_.has_value() || (nearestEnd && !has(pickable, *cursor_))) {
        target = nearestEnd && direction < 0 ? pickable.back() : pickable.front();
    } else if (const auto next = neighbour(*cursor_, direction, pickable); next.has_value()) {
        target = *next;
    } else if (rules.stepAtEndCollapses) {
        target = *cursor_;
    } else {
        return std::nullopt;
    }
    assign({target}, target, target);
    return target;
}

// Range-from-anchor walks the cursor and reselects the whole span, so reversing shrinks it back toward the anchor;
// grow-past-end only ever adds, walking from the selection's end on that side (from the cursor while one dot is
// selected).
template <typename Key>
std::optional<Key> PointSelectionModel<Key>::extend(int direction, const std::vector<Key>& pickable,
                                                    const PointSelectionRules& rules) {
    if (rules.extend == PointSelectionRules::Extend::rangeFromAnchor) {
        if (!cursor_.has_value())
            return step(direction, pickable, rules);
        const auto next = neighbour(*cursor_, direction, pickable);
        if (!next.has_value())
            return std::nullopt;
        const Key anchor = anchor_.value_or(*cursor_);
        std::vector<Key> range;
        for (const Key k : pickable)
            if (!(k < std::min(anchor, *next)) && !(std::max(anchor, *next) < k))
                range.push_back(k);
        assign(std::move(range), next, anchor);
        return next;
    }
    if (selected_.empty())
        return step(direction, pickable, rules);
    Key from = direction > 0 ? selected_.back() : selected_.front();
    if (selected_.size() == 1 && cursor_.has_value())
        from = *cursor_;
    const auto next = neighbour(from, direction, pickable);
    if (!next.has_value())
        return std::nullopt;
    add(*next);
    cursor_ = next;
    return next;
}

// Returns true whenever there was something to select, changed or not: Cmd+A on a fully selected editor is still
// the editor's key.
template <typename Key>
bool PointSelectionModel<Key>::selectAll(const std::vector<Key>& pickable, const PointSelectionRules& rules) {
    if (pickable.empty())
        return false;
    if (rules.selectAllCursor == PointSelectionRules::SelectAllCursor::keepSelectedElseLast) {
        const Key cursor = cursor_.has_value() && contains(*cursor_) ? *cursor_ : pickable.back();
        assign(pickable, cursor, pickable.front());
    } else {
        assign(pickable, cursor_.has_value() ? cursor_ : std::optional<Key>(pickable.front()), anchor_);
    }
    return true;
}

template class PointSelectionModel<int>;
template class PointSelectionModel<double>;

} // namespace synth::ui
