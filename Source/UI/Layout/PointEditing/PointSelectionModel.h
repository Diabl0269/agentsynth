// PointSelectionModel.h -- the selection of every editor with ordered dots (the curve editor, the automation lanes):
// which dots are selected, the keyboard cursor, the anchor a Shift range grows from, and what the selection keys do
// to them (docs/control/point-editing.md). Pure state: no component, no repaint, no doc.
#pragma once

#include <functional>
#include <optional>
#include <vector>

namespace synth::ui {

/** Where the two editors still answer the same key differently; each editor declares one constant of these. The
 *  table in docs/control/point-editing.md#where-the-editors-differ lists them. */
struct PointSelectionRules {
    enum class FirstStep {
        firstPoint, // with no cursor, Left and Right both land on the first dot
        nearestEnd  // with no cursor on a dot, Left lands on the last dot and Right on the first
    };
    enum class Extend {
        rangeFromAnchor, // Shift+arrow moves the cursor and selects every dot between it and the anchor (shrinks too)
        growPastEnd      // Shift+arrow adds the next dot past the selection's end on that side (never shrinks)
    };
    enum class SelectAllCursor {
        keepSelectedElseLast, // Cmd+A keeps a selected cursor, else puts it on the last dot; the anchor goes first
        keepElseFirst         // Cmd+A keeps any cursor, else puts it on the first dot
    };
    FirstStep firstStep = FirstStep::nearestEnd;
    /** A plain arrow at the end collapses the selection onto the cursor (false: it changes nothing). */
    bool stepAtEndCollapses = true;
    Extend extend = Extend::growPastEnd;
    SelectAllCursor selectAllCursor = SelectAllCursor::keepElseFirst;
};

/** `Key` names a dot: its index (the curve editor) or its beat (a lane), ordered like the dots. Every `pickable` list
 *  is the keys a press may land on, ascending. Every call returning bool but selectAll says whether
 *  anything changed. */
template <typename Key>
class PointSelectionModel {
public:
    // ---- The set (ascending), the cursor and the anchor ----
    const std::vector<Key>& selected() const noexcept { return selected_; }
    bool contains(Key key) const;
    bool isEmpty() const noexcept { return selected_.empty(); }
    int size() const noexcept { return (int)selected_.size(); }
    std::optional<Key> cursor() const noexcept { return cursor_; }
    std::optional<Key> anchor() const noexcept { return anchor_; }

    bool clear();
    bool setSelected(std::vector<Key> keys);
    bool setCursor(std::optional<Key> key);
    bool assign(std::vector<Key> keys, std::optional<Key> cursor, std::optional<Key> anchor);
    bool add(Key key);
    bool remove(Key key);
    /** Drops every key not in `alive` from the set, the cursor and the anchor. */
    bool retainOnly(const std::vector<Key>& alive);
    /** Renames every key through `map` (the dots were reordered); `map` must be one-to-one. */
    void rekey(const std::function<Key(Key)>& map);

    // ---- The selection keys ----
    /** Left (-1) / Right (+1): the cursor and a one-dot selection go to the neighbouring dot. Returns the dot landed
     *  on, or nothing when there was no dot to go to (nothing changed). */
    std::optional<Key> step(int direction, const std::vector<Key>& pickable, const PointSelectionRules& rules);
    /** Shift+Left / Shift+Right; returns the dot the cursor reached, or nothing when nothing changed. */
    std::optional<Key> extend(int direction, const std::vector<Key>& pickable, const PointSelectionRules& rules);
    /** Cmd+A: every pickable dot. False when there is none. */
    bool selectAll(const std::vector<Key>& pickable, const PointSelectionRules& rules);

private:
    std::optional<Key> neighbour(Key from, int direction, const std::vector<Key>& pickable) const;

    std::vector<Key> selected_;
    std::optional<Key> cursor_;
    std::optional<Key> anchor_;
};

extern template class PointSelectionModel<int>;
extern template class PointSelectionModel<double>;

} // namespace synth::ui
