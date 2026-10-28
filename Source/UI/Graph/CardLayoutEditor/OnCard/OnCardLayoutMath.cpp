// OnCardLayoutMath.cpp -- snapping a dragged control to its neighbours' lines, pushing crowded neighbours
// aside and finding a free place for a new control. Pure: rectangles in, rectangles out.
#include "OnCardLayoutMath.h"
#include <algorithm>
#include <cstdlib>

namespace synth::ui::oncard {

namespace {

struct AxisSnap {
    bool found = false;
    int delta = 0;
    int position = 0;
    int other = -1; // index into `others`
};

// The smallest move, within the snap distance, that puts one of `edges` on one of `lines`.
void trySnap(AxisSnap& best, const int (&edges)[3], const int (&lines)[3], int otherIndex) {
    for (int edge : edges)
        for (int line : lines) {
            const int delta = line - edge;
            if (std::abs(delta) > kSnapDistance || (best.found && std::abs(delta) >= std::abs(best.delta)))
                continue;
            best = {true, delta, line, otherIndex};
        }
}

Guide guideFor(bool vertical, int position, juce::Rectangle<int> a, juce::Rectangle<int> b) {
    const auto both = a.getUnion(b);
    return vertical ? Guide{true, position, both.getY(), both.getBottom()}
                    : Guide{false, position, both.getX(), both.getRight()};
}

// Where `cell` goes to clear `anchor` by the gap in each direction, as the new top-left.
juce::Point<int> candidate(int direction, juce::Rectangle<int> cell, juce::Rectangle<int> anchor) {
    switch (direction) {
    case 0:
        return {anchor.getX() - kControlGap - cell.getWidth(), cell.getY()};
    case 1:
        return {anchor.getRight() + kControlGap, cell.getY()};
    case 2:
        return {cell.getX(), anchor.getY() - kControlGap - cell.getHeight()};
    default:
        return {cell.getX(), anchor.getBottom() + kControlGap};
    }
}

bool fitsLimits(juce::Rectangle<int> rect, const Limits& limits) {
    return rect.getX() >= limits.minX && rect.getRight() <= limits.maxX && rect.getY() >= limits.top;
}

// `cell` moved clear of `anchor` the shortest way that stays inside the limits and clear of every
// `fixed` rect; below everything fixed when no direction qualifies.
juce::Rectangle<int> pushOut(juce::Rectangle<int> cell, juce::Rectangle<int> anchor,
                             const std::vector<juce::Rectangle<int>>& fixed, const Limits& limits) {
    juce::Rectangle<int> best;
    int bestDistance = 0;
    bool found = false;
    for (int direction = 0; direction < 4; ++direction) {
        const auto moved = cell.withPosition(candidate(direction, cell, anchor));
        const bool clear = std::none_of(fixed.begin(), fixed.end(), [&](const auto& r) { return tooClose(moved, r); });
        const int distance = std::abs(moved.getX() - cell.getX()) + std::abs(moved.getY() - cell.getY());
        if (!fitsLimits(moved, limits) || !clear || (found && distance >= bestDistance))
            continue;
        best = moved;
        bestDistance = distance;
        found = true;
    }
    if (found)
        return best;
    int bottom = anchor.getBottom();
    for (const auto& r : fixed)
        bottom = std::max(bottom, r.getBottom());
    return cell.withY(bottom + kControlGap);
}

using Rect = juce::Rectangle<int>;
using Rects = std::vector<Rect>;
using Ids = std::vector<int>;

int bottomOf(const Rects& all, const Ids& ids) {
    int bottom = all[(size_t)ids.front()].getBottom();
    for (int id : ids)
        bottom = std::max(bottom, all[(size_t)id].getBottom());
    return bottom;
}

int topOf(const Rects& all, const Ids& ids) {
    int top = all[(size_t)ids.front()].getY();
    for (int id : ids)
        top = std::min(top, all[(size_t)id].getY());
    return top;
}

// Cells left to right, each at least the gap clear of the one before; if the last then stands past maxX, the
// row is pulled back from there.
void sweepRow(Rects& all, const Ids& ids, const Limits& limits, int gap) {
    int edge = limits.minX - gap;
    for (int id : ids) {
        auto& r = all[(size_t)id];
        r.setX(std::max(r.getX(), edge + gap));
        edge = r.getRight();
    }
    if (ids.empty() || all[(size_t)ids.back()].getRight() <= limits.maxX)
        return;
    int right = limits.maxX;
    for (auto id = ids.rbegin(); id != ids.rend(); ++id) {
        auto& r = all[(size_t)*id];
        r.setX(std::min(r.getX(), right - r.getWidth()));
        right = r.getX() - gap;
    }
}

// The trailing cells (taken out of `ids`) the row cannot hold in the content width.
Ids wrapTrailing(const Rects& all, Ids& ids, const Limits& limits, int gap) {
    const auto width = [&] {
        int total = gap * std::max(0, (int)ids.size() - 1);
        for (int id : ids)
            total += all[(size_t)id].getWidth();
        return total;
    };
    Ids overflow;
    while (ids.size() > 1 && width() > limits.maxX - limits.minX) {
        overflow.insert(overflow.begin(), ids.back());
        ids.pop_back();
    }
    return overflow;
}

// The space a row keeps between its cells: the gap, or less where the row already stands closer (a default layout
// has its cells flush), so a row that was full is still full once a cell has left and come back. `skip` is a cell
// not counted (one that is being resized).
int rowGap(const Rects& all, const Ids& ids, int skip) {
    int gap = kControlGap;
    for (size_t i = 1; i < ids.size(); ++i)
        if (ids[i - 1] != skip && ids[i] != skip)
            gap = std::min(gap, std::max(0, all[(size_t)ids[i]].getX() - all[(size_t)ids[i - 1]].getRight()));
    return gap;
}

// The row laid out (its overflow wrapped into rows below it, left-aligned); the bottom of what it now takes.
int placeRow(Rects& all, Ids ids, const Limits& limits, int gap) {
    auto overflow = wrapTrailing(all, ids, limits, gap);
    sweepRow(all, ids, limits, gap);
    const int bottom = bottomOf(all, ids);
    if (overflow.empty())
        return bottom;
    int x = limits.minX;
    for (int id : overflow) {
        auto& r = all[(size_t)id];
        r.setPosition(x, bottom + kControlGap);
        x += r.getWidth() + gap;
    }
    return placeRow(all, std::move(overflow), limits, gap);
}

// The rows from `first` on, each moved down only when it now collides with what is above it, and then far
// enough to leave the gap (rows that stand flush and are not hit stay flush).
void shiftRowsBelow(Rects& all, const std::vector<Ids>& rows, size_t first, int bottom) {
    for (size_t i = first; i < rows.size(); ++i) {
        const int top = topOf(all, rows[i]);
        const int shift = bottom > top ? bottom + kControlGap - top : 0;
        for (int id : rows[i])
            all[(size_t)id].translate(0, shift);
        bottom = bottomOf(all, rows[i]);
    }
}

} // namespace

juce::String describeMove(const juce::String& caption, int dx, int dy) {
    juce::StringArray parts;
    if (dx != 0)
        parts.add((dx > 0 ? "right " : "left ") + juce::String(std::abs(dx)));
    if (dy != 0)
        parts.add((dy > 0 ? "down " : "up ") + juce::String(std::abs(dy)));
    return caption + " moved " + parts.joinIntoString(", ");
}

bool tooClose(juce::Rectangle<int> a, juce::Rectangle<int> b) { return a.expanded(kControlGap).intersects(b); }

SnapResult snapDraggedRect(juce::Rectangle<int> dragged, const std::vector<juce::Rectangle<int>>& others,
                           bool enabled) {
    SnapResult result{dragged, {}};
    if (!enabled)
        return result;
    AxisSnap x;
    AxisSnap y;
    const int xEdges[3] = {dragged.getX(), dragged.getCentreX(), dragged.getRight()};
    const int yEdges[3] = {dragged.getY(), dragged.getCentreY(), dragged.getBottom()};
    for (int i = 0; i < (int)others.size(); ++i) {
        const auto& o = others[(size_t)i];
        trySnap(x, xEdges, {o.getX(), o.getCentreX(), o.getRight()}, i);
        trySnap(y, yEdges, {o.getY(), o.getCentreY(), o.getBottom()}, i);
    }
    result.rect = dragged.translated(x.found ? x.delta : 0, y.found ? y.delta : 0);
    if (x.found)
        result.guides.push_back(guideFor(true, x.position, result.rect, others[(size_t)x.other]));
    if (y.found)
        result.guides.push_back(guideFor(false, y.position, result.rect, others[(size_t)y.other]));
    return result;
}

juce::Rectangle<int> clampToLimits(juce::Rectangle<int> rect, const Limits& limits) {
    const int maxX = std::max(limits.minX, limits.maxX - rect.getWidth());
    return rect.withPosition(juce::jlimit(limits.minX, maxX, rect.getX()), std::max(limits.top, rect.getY()));
}

// Each cell is pushed once, by the dropped one or by a cell already pushed; a pushed cell then crowds
// whatever it landed near the same way.
std::vector<juce::Rectangle<int>> pushAside(juce::Rectangle<int> dropped, juce::Rectangle<int> start,
                                            const std::vector<juce::Rectangle<int>>& others, const Limits& limits) {
    auto result = others;
    std::vector<bool> pushed(others.size(), false);
    std::vector<juce::Rectangle<int>> fixed{dropped};
    std::vector<juce::Rectangle<int>> queue{dropped};
    bool first = true;
    while (!queue.empty()) {
        const auto anchor = queue.back();
        queue.pop_back();
        for (size_t i = 0; i < result.size(); ++i) {
            const bool flushAtStart = first && tooClose(start, result[i]) && !anchor.intersects(result[i]);
            if (pushed[i] || flushAtStart || !tooClose(anchor, result[i]))
                continue;
            result[i] = pushOut(result[i], anchor, fixed, limits);
            pushed[i] = true;
            fixed.push_back(result[i]);
            queue.push_back(result[i]);
        }
        first = false;
    }
    return result;
}

std::vector<juce::Rectangle<int>> returnHome(std::vector<juce::Rectangle<int>> cells,
                                             const std::vector<juce::Rectangle<int>>& homes,
                                             const std::vector<juce::Rectangle<int>>& origins,
                                             juce::Rectangle<int> dropped, juce::Rectangle<int> droppedOrigin) {
    const auto free = [&](size_t i) {
        const bool atOrigin = homes[i] == origins[i];
        if (tooClose(homes[i], dropped) && !(atOrigin && dropped == droppedOrigin))
            return false;
        for (size_t j = 0; j < cells.size(); ++j)
            if (j != i && tooClose(homes[i], cells[j]) && !(atOrigin && cells[j] == origins[j]))
                return false;
        return true;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < cells.size(); ++i)
            if (cells[i] != homes[i] && free(i)) {
                cells[i] = homes[i];
                changed = true;
            }
    }
    return cells;
}

std::vector<std::vector<int>> rowsOf(const std::vector<Rect>& cells) {
    Ids order(cells.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const auto& ra = cells[(size_t)a];
        const auto& rb = cells[(size_t)b];
        return ra.getY() != rb.getY() ? ra.getY() < rb.getY() : ra.getX() < rb.getX();
    });
    std::vector<Ids> rows;
    int rowBottom = 0;
    for (int id : order) {
        const auto& r = cells[(size_t)id];
        if (rows.empty() || r.getY() >= rowBottom) {
            rows.emplace_back();
            rowBottom = r.getBottom();
        }
        rows.back().push_back(id);
        rowBottom = std::max(rowBottom, r.getBottom());
    }
    for (auto& row : rows)
        std::stable_sort(row.begin(), row.end(),
                         [&](int a, int b) { return cells[(size_t)a].getX() < cells[(size_t)b].getX(); });
    return rows;
}

RowDrop dropIntoRow(Rect dropped, juce::Point<int> probe, const Rects& others, const Limits& limits) {
    const int n = (int)others.size();
    Rects all = others;
    all.push_back(oncard::clampToLimits(dropped, limits));
    const auto rows = rowsOf(others);
    RowDrop out;
    if (!rows.empty()) {
        // A row owns the probe down to the middle of the gap under it (the last row, a hair below its bottom).
        const auto ownedEnd = [&](size_t i) {
            const int bottom = bottomOf(all, rows[i]);
            return i + 1 < rows.size() ? (bottom + topOf(all, rows[i + 1])) / 2 : bottom + kControlGap / 4;
        };
        size_t r = 0;
        while (r < rows.size() && probe.y >= ownedEnd(r))
            ++r;
        if (r < rows.size() && probe.y >= topOf(all, rows[r])) {
            Ids ids = rows[r];
            const auto at =
                std::count_if(ids.begin(), ids.end(), [&](int id) { return all[(size_t)id].getCentreX() < probe.x; });
            all.back().setY(topOf(all, ids));
            ids.insert(ids.begin() + at, n);
            const int gap = rowGap(all, ids, n);
            shiftRowsBelow(all, rows, r + 1, placeRow(all, std::move(ids), limits, gap));
        } else {
            // Below the last row, between two rows or above the first: a row of its own, against the row above.
            all.back().setY(r > 0 ? bottomOf(all, rows[r - 1]) + kControlGap : limits.top);
            out.newRowAt = all.back().getY();
            shiftRowsBelow(all, rows, r, all.back().getBottom());
        }
    }
    out.dropped = all.back();
    all.pop_back();
    out.others = std::move(all);
    return out;
}

RowDrop dropIntoRow(Rect dropped, const Rects& others, const Limits& limits) {
    return dropIntoRow(dropped, oncard::clampToLimits(dropped, limits).getCentre(), others, limits);
}

Rects repackRow(const Rects& cells, size_t changed, Rect before, const Limits& limits) {
    Rects all = cells;
    const auto rows = rowsOf(cells);
    for (size_t r = 0; r < rows.size(); ++r) {
        const auto& ids = rows[r];
        if (std::find(ids.begin(), ids.end(), (int)changed) == ids.end())
            continue;
        const int delta = cells[changed].getWidth() - before.getWidth();
        int oldRight = before.getRight();
        bool after = false;
        for (int id : ids) {
            after = after || id == (int)changed;
            auto& cell = all[(size_t)id];
            if (!after || id == (int)changed || delta >= 0 || cell.getX() - oldRight > kControlGap)
                continue;
            oldRight = cell.getRight();
            cell.translate(delta, 0);
        }
        shiftRowsBelow(all, rows, r + 1, placeRow(all, ids, limits, rowGap(all, ids, (int)changed)));
        break;
    }
    return all;
}

// The candidate corners are the limits' own and every occupied rectangle's right and bottom edge plus the
// gap: any free place can slide up and left until it rests on one of them.
juce::Point<int> findFreeSpot(juce::Point<int> size, const std::vector<juce::Rectangle<int>>& occupied,
                              const Limits& limits) {
    std::vector<int> xs{limits.minX};
    std::vector<int> ys{limits.top};
    for (const auto& r : occupied) {
        xs.push_back(r.getRight() + kControlGap);
        ys.push_back(r.getBottom() + kControlGap);
    }
    std::sort(ys.begin(), ys.end());
    std::sort(xs.begin(), xs.end());
    for (int y : ys)
        for (int x : xs) {
            const juce::Rectangle<int> rect(x, y, size.x, size.y);
            const bool fits = x == limits.minX || rect.getRight() <= limits.maxX;
            if (fits &&
                std::none_of(occupied.begin(), occupied.end(), [&](const auto& r) { return tooClose(rect, r); }))
                return {x, y};
        }
    return {limits.minX, ys.back()};
}

} // namespace synth::ui::oncard
