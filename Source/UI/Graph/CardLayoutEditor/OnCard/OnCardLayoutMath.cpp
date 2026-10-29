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

namespace {

// The cells of `all` that stand in the vertical span of `band`.
bool anyInBand(const Rects& all, Rect band) {
    return std::any_of(all.begin(), all.end(),
                       [&](const Rect& r) { return r.getY() < band.getBottom() && r.getBottom() > band.getY(); });
}

// Where `band` stood a row stood that is now empty: the rows below it move up to take its place.
void closeBand(Rects& all, Rect band) {
    int nextTop = 0;
    bool below = false;
    for (const auto& r : all)
        if (r.getY() >= band.getBottom() && (!below || r.getY() < nextTop)) {
            nextTop = r.getY();
            below = true;
        }
    if (!below)
        return;
    for (auto& r : all)
        if (r.getY() >= band.getBottom())
            r.translate(0, band.getY() - nextTop);
}

// The cells (not in `ids`) displaced from a home on a row spanning `top`..`bottom` that `ids` could hold with
// them: all of them or none. Each is put at its home's x, so the sweep keeps their order.
Ids returningCells(Rects& all, const Ids& ids, int top, int bottom, const Rects& homes, const Limits& limits, int gap) {
    Ids back;
    int width = 0;
    for (int id : ids)
        width += all[(size_t)id].getWidth() + gap;
    for (size_t k = 0; k < homes.size(); ++k) {
        const auto& home = homes[k];
        if (home == all[k] || home.isEmpty() || home.getY() >= bottom || home.getBottom() <= top ||
            std::find(ids.begin(), ids.end(), (int)k) != ids.end())
            continue;
        back.push_back((int)k);
        width += all[k].getWidth() + gap;
    }
    if (width - gap > limits.maxX - limits.minX)
        return {};
    for (int id : back) {
        all[(size_t)id].setPosition(homes[(size_t)id].getX(), top);
    }
    return back;
}

// The rows below `first` without the cells now elsewhere (`moved`); a row that lost all its cells is gone.
std::vector<Ids> withoutMoved(const std::vector<Ids>& rows, size_t first, const Ids& moved) {
    std::vector<Ids> kept;
    for (size_t i = first; i < rows.size(); ++i) {
        Ids row;
        for (int id : rows[i])
            if (std::find(moved.begin(), moved.end(), id) == moved.end())
                row.push_back(id);
        if (!row.empty())
            kept.push_back(std::move(row));
    }
    return kept;
}

// `ids` (the row `r` the dropped cell joins) with the displaced cells whose home is on it, laid out; the rows
// under it move down where it now collides with them.
void joinRow(Rects& all, std::vector<Ids> rows, size_t r, int dropped, juce::Point<int> probe, const Limits& limits,
             const DropContext& context, std::vector<Rect>& vacated, const Rects& baseline) {
    Ids ids = rows[r];
    const auto at =
        std::count_if(ids.begin(), ids.end(), [&](int id) { return all[(size_t)id].getCentreX() < probe.x; });
    const int rowTop = topOf(all, ids);
    all[(size_t)dropped].setY(rowTop);
    ids.insert(ids.begin() + at, dropped);
    const int rowBottom = std::max(bottomOf(all, ids), rowTop + all[(size_t)dropped].getHeight());
    // A control that began on this row keeps its flush spacing; one from another group is spaced with the gap.
    const bool native = !context.vacated.isEmpty() || (!context.origin.isEmpty() && context.origin.getY() < rowBottom &&
                                                       context.origin.getBottom() > rowTop);
    const int gap = native ? rowGap(all, ids, dropped) : kControlGap;
    const auto back = returningCells(all, ids, rowTop, rowBottom, context.homes, limits, gap);
    for (int id : back) {
        vacated.push_back(baseline[(size_t)id]);
        ids.push_back(id);
    }
    std::stable_sort(ids.begin(), ids.end(),
                     [&](int a, int b) { return all[(size_t)a].getCentreX() < all[(size_t)b].getCentreX(); });
    const auto below = withoutMoved(rows, r + 1, back);
    shiftRowsBelow(all, below, 0, placeRow(all, std::move(ids), limits, gap));
}

} // namespace

namespace {

// A control let go on the spot where the layout first put it goes back there exactly, and every cell that stands off
// its home returns to it where that is free: dragging out and back is the layout it began as, not one with the
// row gap in it. Nothing is returned (and the normal plan runs) when the spot is taken or its row would be left empty.
bool returnToOrigin(Rect dropped, juce::Point<int> probe, const Rects& others, const DropContext& context,
                    RowDrop& out) {
    const auto& origin = context.origin;
    if (origin.isEmpty() || context.vacated == origin || !origin.contains(probe) ||
        context.homes.size() != others.size() || origin.getWidth() != dropped.getWidth() ||
        origin.getHeight() != dropped.getHeight())
        return false;
    Rects all = others;
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t k = 0; k < all.size(); ++k) {
            const auto& home = context.homes[k];
            const bool free = !home.isEmpty() && !home.intersects(origin) &&
                              std::none_of(all.begin(), all.end(),
                                           [&](const Rect& r) { return &r != &all[k] && r.intersects(home); });
            if (all[k] != home && free) {
                all[k] = home;
                changed = true;
            }
        }
    }
    if (std::any_of(all.begin(), all.end(), [&](const Rect& r) { return r.intersects(origin); }) ||
        (!context.vacated.isEmpty() && !anyInBand(all, context.vacated)))
        return false;
    out.dropped = origin;
    out.others = std::move(all);
    return true;
}

} // namespace

RowDrop dropIntoRow(Rect dropped, juce::Point<int> probe, const Rects& others, const Limits& limits,
                    const DropContext& context) {
    if (RowDrop back; returnToOrigin(dropped, probe, others, context, back))
        return back;
    const int n = (int)others.size();
    const Rects baseline = others;
    Rects all = others;
    all.push_back(oncard::clampToLimits(dropped, limits));
    const auto rows = rowsOf(others);
    std::vector<Rect> vacated;
    if (!context.vacated.isEmpty())
        vacated.push_back(context.vacated);
    RowDrop out;
    if (!rows.empty()) {
        // A row owns the probe down to the middle of the gap under it (the last row, a hair below its bottom).
        const auto ownedEnd = [&](size_t i) {
            const int bottom = bottomOf(all, rows[i]);
            return i + 1 < rows.size() ? (bottom + topOf(all, rows[i + 1])) / 2 : bottom + kControlGap / 4;
        };
        // The band the dropped control left, when it stood alone on its row: the pointer over it keeps it there.
        const bool inOwnBand = !context.vacated.isEmpty() && !anyInBand(others, context.vacated) &&
                               probe.y >= context.vacated.getY() && probe.y < context.vacated.getBottom();
        size_t r = 0;
        while (r < rows.size() &&
               (inOwnBand ? topOf(all, rows[r]) < context.vacated.getBottom() : probe.y >= ownedEnd(r)))
            ++r;
        if (!inOwnBand && r < rows.size() && probe.y >= topOf(all, rows[r])) {
            joinRow(all, rows, r, n, probe, limits, context, vacated, baseline);
        } else {
            // Below the last row, between two rows or above the first: a row of its own, against the row above (in
            // the band the control left, where it stood).
            const int above = r > 0 ? bottomOf(all, rows[r - 1]) + kControlGap : limits.top;
            all.back().setY(inOwnBand ? context.vacated.getY() : above);
            out.newRowAt = all.back().getY();
            shiftRowsBelow(all, rows, r, all.back().getBottom());
        }
    }
    std::sort(vacated.begin(), vacated.end(), [](const Rect& a, const Rect& b) { return a.getY() > b.getY(); });
    for (const auto& band : vacated)
        if (!anyInBand(all, band))
            closeBand(all, band);
    out.dropped = all.back();
    if (out.newRowAt >= 0)
        out.newRowAt = out.dropped.getY();
    all.pop_back();
    out.others = std::move(all);
    return out;
}

Rects leaveGroup(const Rects& cells, const Rects& homes, Rect vacated) {
    Rects all = cells;
    const auto free = [&](size_t k) {
        for (size_t j = 0; j < all.size(); ++j)
            if (j != k && all[j].intersects(homes[k]))
                return false;
        return true;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t k = 0; k < all.size() && k < homes.size(); ++k)
            if (!homes[k].isEmpty() && all[k] != homes[k] && free(k)) {
                all[k] = homes[k];
                changed = true;
            }
    }
    if (!vacated.isEmpty() && !anyInBand(all, vacated))
        closeBand(all, vacated);
    return all;
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
