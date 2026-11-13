#include "LayoutUtil.h"
#include "Modules/ModuleBase.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace synth::LayoutUtil {

//==============================================================================
// Module width buckets
//==============================================================================

ModuleWidthBucket getModuleWidthBucket(ModuleType t) {
    switch (t) {
    case ModuleType::Sequencer:
    case ModuleType::PolySequencer:
    case ModuleType::MidiKeyboard:
    case ModuleType::MacroControl: // its body sits beside the jack gutter, so four knobs across fit
    // Parametric EQ needs the extra width for a readable response curve plus four band rows of
    // On / Freq / Gain / Q laid out side by side.
    case ModuleType::ParametricEQ:
        return ModuleWidthBucket::Double;
    case ModuleType::Attenuverter:
        return ModuleWidthBucket::Narrow;
    default:
        return ModuleWidthBucket::Single;
    }
}

int moduleWidth(ModuleWidthBucket b) {
    switch (b) {
    case ModuleWidthBucket::Narrow:
        return kNarrowWidth;
    case ModuleWidthBucket::Double:
        return kDoubleWidth;
    default:
        return kSingleWidth;
    }
}

int moduleWidth(ModuleType t) { return moduleWidth(getModuleWidthBucket(t)); }

//==============================================================================
// snap
//==============================================================================
int snap(int v) { return (int)(std::lround(v / (double)kGridSize) * kGridSize); }

juce::Point<int> snap(juce::Point<int> p) { return {snap(p.x), snap(p.y)}; }

//==============================================================================
// intersectsAny
//==============================================================================
bool intersectsAny(const juce::Rectangle<int>& candidate, const std::vector<Box>& others, NodeID selfId, int gap) {
    // Enforce a minimum clear gap of `gap` by inflating ONLY the candidate and testing against the raw
    // other boxes. Inflating BOTH would double the enforced clearance to 2*gap, which wrongly rejects
    // layouts that are intentionally only `gap`+ apart (e.g. preset columns ~20px apart get bumped).
    auto inflated = candidate.expanded(gap);
    for (const auto& box : others) {
        if (box.id == selfId)
            continue;
        ++freeSlotSearchCountersForTest().boxTests;
        if (inflated.intersects(box.rect))
            return true;
    }
    return false;
}

//==============================================================================
// findFreeSlot
//==============================================================================
namespace {

FreeSlotSearchCounters searchCounters;

// The boxes of one findFreeSlot call, filed by the square cells of a grid laid over them, so a candidate is tested
// only against the boxes in the cells it touches: the search tries up to every spot of 256 rings, and testing each
// against every box made placing one module on a big canvas cost spots x boxes (most of an add on the Load test
// project). A box is in every cell it overlaps; the answer is the same as intersectsAny's.
class BoxGrid {
public:
    BoxGrid(const std::vector<Box>& boxes, NodeID selfId)
        : boxes_(boxes) {
        bool any = false;
        for (const auto& box : boxes)
            if (box.id != selfId && !box.rect.isEmpty()) {
                area_ = any ? area_.getUnion(box.rect) : box.rect;
                any = true;
            }
        if (!any)
            return;
        constexpr int kMostCellsPerSide = 256;
        cell_ = juce::jmax(kMinCell, (juce::jmax(area_.getWidth(), area_.getHeight()) + kMostCellsPerSide - 1) /
                                         kMostCellsPerSide);
        cols_ = area_.getWidth() / cell_ + 1;
        rows_ = area_.getHeight() / cell_ + 1;
        cells_.resize((size_t)(cols_ * rows_));
        for (int i = 0; i < (int)boxes.size(); ++i) {
            const auto& box = boxes[(size_t)i];
            if (box.id == selfId || box.rect.isEmpty())
                continue;
            const auto span = cellsOf(box.rect);
            for (int r = span.getY(); r <= span.getBottom(); ++r)
                for (int c = span.getX(); c <= span.getRight(); ++c)
                    cells_[(size_t)(r * cols_ + c)].push_back(i);
        }
    }

    bool intersects(const juce::Rectangle<int>& candidate, int gap) const {
        const auto inflated = candidate.expanded(gap); // as intersectsAny: only the candidate is inflated
        if (cells_.empty() || !inflated.intersects(area_))
            return false;
        const auto span = cellsOf(inflated.getIntersection(area_));
        for (int r = span.getY(); r <= span.getBottom(); ++r)
            for (int c = span.getX(); c <= span.getRight(); ++c)
                for (const int i : cells_[(size_t)(r * cols_ + c)]) {
                    ++searchCounters.boxTests;
                    if (inflated.intersects(boxes_[(size_t)i].rect))
                        return true;
                }
        return false;
    }

private:
    static constexpr int kMinCell = 256;

    // The first and last column and row `r` (inside area_) covers; getRight()/getBottom() are inclusive here.
    juce::Rectangle<int> cellsOf(const juce::Rectangle<int>& r) const {
        const int c0 = juce::jlimit(0, cols_ - 1, (r.getX() - area_.getX()) / cell_);
        const int r0 = juce::jlimit(0, rows_ - 1, (r.getY() - area_.getY()) / cell_);
        const int c1 = juce::jlimit(0, cols_ - 1, (r.getRight() - 1 - area_.getX()) / cell_);
        const int r1 = juce::jlimit(0, rows_ - 1, (r.getBottom() - 1 - area_.getY()) / cell_);
        return {c0, r0, c1 - c0, r1 - r0};
    }

    const std::vector<Box>& boxes_;
    juce::Rectangle<int> area_;
    int cell_ = kMinCell, cols_ = 0, rows_ = 0;
    std::vector<std::vector<int>> cells_;
};

} // namespace

FreeSlotSearchCounters& freeSlotSearchCountersForTest() noexcept { return searchCounters; }

juce::Point<int> findFreeSlot(juce::Point<int> desired, int w, int h, const std::vector<Box>& others, NodeID selfId,
                              int gap) {
    auto clamp = [&](juce::Point<int> p) -> juce::Point<int> {
        return {juce::jmax(0, p.x), juce::jmax(0, p.y)}; // the origin is the wall; there is no right/bottom limit
    };

    auto snapped = snap(desired);
    snapped = clamp(snapped);

    const BoxGrid grid(others, selfId);
    if (!grid.intersects({snapped.x, snapped.y, w, h}, gap))
        return snapped;

    // Square spiral search around the desired point. Every spot of one ring is as far as the next by the ring, but not
    // in a straight line: the first clear spot in walk order can be a corner (beside and below a wide open macro) while
    // the spot straight below is as near by the ring, so a ring's clear spots are compared and the nearest one wins.
    for (int ring = 1; ring <= kSpiralMaxRings; ++ring) {
        const int step = kSpiralStep * ring;
        std::optional<juce::Point<int>> best;
        int64_t bestDist = 0;
        auto consider = [&](int dx, int dy) {
            const auto pt = clamp(snap(juce::Point<int>{snapped.x + dx, snapped.y + dy}));
            if (grid.intersects({pt.x, pt.y, w, h}, gap))
                return;
            const int64_t ex = pt.x - snapped.x, ey = pt.y - snapped.y;
            if (!best.has_value() || ex * ex + ey * ey < bestDist) {
                best = pt;
                bestDist = ex * ex + ey * ey;
            }
        };
        // The perimeter of the ring, in the old walk order (top, right, bottom, left), which breaks ties.
        for (int dx = -step; dx <= step; dx += kSpiralStep)
            consider(dx, -step);
        for (int dy = -step + kSpiralStep; dy <= step; dy += kSpiralStep)
            consider(step, dy);
        for (int dx = step - kSpiralStep; dx >= -step; dx -= kSpiralStep)
            consider(dx, step);
        for (int dy = step - kSpiralStep; dy >= -step + kSpiralStep; dy -= kSpiralStep)
            consider(-step, dy);
        if (best.has_value())
            return *best;
    }

    // Give up: return snapped+clamped desired
    return snapped;
}

//==============================================================================
// findFreeSlotBelow
//==============================================================================
juce::Point<int> findFreeSlotBelow(juce::Point<int> desired, int w, int h, const std::vector<Box>& others, int gap) {
    auto p = snap(desired);
    p = {juce::jmax(0, p.x), juce::jmax(0, p.y)};
    for (int step = 0; step < kWalkDownMaxSteps; ++step, p.y += kGridSize)
        if (!intersectsAny({p.x, p.y, w, h}, others, NodeID{}, gap))
            return p;
    return p;
}

//==============================================================================
// resolveDisplacement
//==============================================================================
namespace {

enum class Dir { Down, Right, Up, Left, None }; // declaration order == tie-break order

struct Push {
    Dir dir = Dir::None;
    int amount = 0; // penetration, before grid snapping
};

int snapUp(int v) { return ((v + kGridSize - 1) / kGridSize) * kGridSize; }

juce::Point<int> deltaFor(Dir d, int amount) {
    const int a = snapUp(amount);
    switch (d) {
    case Dir::Down:
        return {0, a};
    case Dir::Right:
        return {a, 0};
    case Dir::Up:
        return {0, -a};
    case Dir::Left:
        return {-a, 0};
    case Dir::None:
        break;
    }
    return {};
}

// How far `u` must travel in direction `d` to sit `gap` clear of `p`.
int penetration(Dir d, const juce::Rectangle<int>& p, const juce::Rectangle<int>& u, int gap) {
    switch (d) {
    case Dir::Down:
        return p.getBottom() + gap - u.getY();
    case Dir::Right:
        return p.getRight() + gap - u.getX();
    case Dir::Up:
        return u.getBottom() - (p.getY() - gap);
    case Dir::Left:
        return u.getRight() - (p.getX() - gap);
    case Dir::None:
        break;
    }
    return 0;
}

// Whether `u` lies wholly on the `d` side of the rect the grower occupied before it grew.
bool sideOfPreviousRect(Dir d, const juce::Rectangle<int>& was, const juce::Rectangle<int>& u) {
    switch (d) {
    case Dir::Down:
        return u.getY() >= was.getBottom();
    case Dir::Right:
        return u.getX() >= was.getRight();
    case Dir::Up:
        return u.getBottom() <= was.getY();
    case Dir::Left:
        return u.getRight() <= was.getX();
    case Dir::None:
        break;
    }
    return false;
}

// The wall (canvas top-left) and pinned units both block a destination.
bool blocked(const juce::Rectangle<int>& dest, const std::vector<LayoutUnit>& units, const std::vector<bool>& isMoved,
             const std::vector<juce::Rectangle<int>>& cur, size_t self) {
    if (dest.getX() < 0 || dest.getY() < 0)
        return true;
    for (size_t i = 0; i < units.size(); ++i)
        if (i != self && units[i].pinned && !isMoved[i] && cur[i].intersects(dest))
            return true;
    return false;
}

} // namespace

std::vector<UnitMove> resolveDisplacement(const juce::String& growerKey, const std::vector<LayoutUnit>& units,
                                          int gap) {
    const size_t n = units.size();
    std::vector<juce::Rectangle<int>> cur(n);
    std::vector<bool> isMoved(n, false);
    std::optional<size_t> grower;
    for (size_t i = 0; i < n; ++i) {
        cur[i] = units[i].rect;
        if (units[i].key == growerKey)
            grower = i;
    }
    if (!grower)
        return {};

    struct Pusher {
        size_t idx;
        Dir inherited;
    };
    auto overlaps = [&](size_t pusher, size_t u) { return cur[pusher].expanded(gap).intersects(cur[u]); };

    std::vector<Pusher> frontier{{*grower, Dir::None}};
    int rounds = 0;
    bool regrow = true;
    while (regrow && rounds < kDisplacementMaxRounds) {
        regrow = false;
        while (!frontier.empty() && rounds++ < kDisplacementMaxRounds) {
            std::vector<Pusher> next;
            for (const auto& pusher : frontier) {
                // Everything this pusher currently overlaps, most-penetrated first (deterministic ties).
                struct Hit {
                    size_t idx;
                    int pen;
                };
                std::vector<Hit> hits;
                for (size_t u = 0; u < n; ++u) {
                    if (u == pusher.idx || u == *grower || units[u].pinned || !overlaps(pusher.idx, u))
                        continue;
                    int least = std::numeric_limits<int>::max();
                    for (auto d : {Dir::Down, Dir::Right, Dir::Up, Dir::Left})
                        least = std::min(least, penetration(d, cur[pusher.idx], cur[u], gap));
                    hits.push_back({u, least});
                }
                std::sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
                    if (a.pen != b.pen)
                        return a.pen > b.pen;
                    if (cur[a.idx].getY() != cur[b.idx].getY())
                        return cur[a.idx].getY() < cur[b.idx].getY();
                    if (cur[a.idx].getX() != cur[b.idx].getX())
                        return cur[a.idx].getX() < cur[b.idx].getX();
                    return units[a.idx].key < units[b.idx].key;
                });

                for (const auto& hit : hits) {
                    const size_t u = hit.idx;
                    if (!overlaps(pusher.idx, u))
                        continue; // an earlier push in this pass already cleared it
                    auto canGo = [&](Dir d) {
                        const int pen = penetration(d, cur[pusher.idx], cur[u], gap);
                        return pen > 0 && !blocked(cur[u].translated(deltaFor(d, pen).x, deltaFor(d, pen).y), units,
                                                   isMoved, cur, u);
                    };

                    Dir chosen = Dir::None;
                    if (pusher.inherited != Dir::None) {
                        if (canGo(pusher.inherited))
                            chosen = pusher.inherited;
                    } else {
                        Dir best = Dir::None;
                        int bestPen = std::numeric_limits<int>::max();
                        const auto& was = units[*grower].before;
                        bool sided = false;
                        if (pusher.idx == *grower && !was.isEmpty())
                            for (auto d : {Dir::Down, Dir::Right, Dir::Up, Dir::Left})
                                sided = sided || sideOfPreviousRect(d, was, cur[u]);
                        for (auto d : {Dir::Down, Dir::Right, Dir::Up, Dir::Left}) {
                            if (sided && !sideOfPreviousRect(d, was, cur[u]))
                                continue;
                            const int pen = penetration(d, cur[pusher.idx], cur[u], gap);
                            if (pen < bestPen) {
                                bestPen = pen;
                                best = d;
                            }
                        }
                        if (canGo(best))
                            chosen = best;
                    }
                    if (chosen == Dir::None) {
                        // Blocked: the smaller of the two positive directions (tie: down).
                        const int penD = penetration(Dir::Down, cur[pusher.idx], cur[u], gap);
                        const int penR = penetration(Dir::Right, cur[pusher.idx], cur[u], gap);
                        const std::array<Dir, 2> order = penD <= penR ? std::array<Dir, 2>{Dir::Down, Dir::Right}
                                                                      : std::array<Dir, 2>{Dir::Right, Dir::Down};
                        for (auto d : order)
                            if (canGo(d)) {
                                chosen = d;
                                break;
                            }
                    }
                    if (chosen == Dir::None)
                        continue; // boxed in on both sides: leave it

                    const auto delta = deltaFor(chosen, penetration(chosen, cur[pusher.idx], cur[u], gap));
                    cur[u].translate(delta.x, delta.y);
                    isMoved[u] = true;
                    next.push_back({u, chosen});
                }
            }
            frontier = std::move(next);
        }

        // A pushed unit may have been carried back over the grower: run the grower again until it is clear.
        for (size_t u = 0; u < n; ++u)
            if (u != *grower && !units[u].pinned && overlaps(*grower, u) && isMoved[u]) {
                regrow = true;
                break;
            }
        if (regrow)
            frontier = {{*grower, Dir::None}};
    }

    std::vector<UnitMove> moves;
    for (size_t i = 0; i < n; ++i) {
        const auto delta = cur[i].getPosition() - units[i].rect.getPosition();
        if (isMoved[i] && (delta.x != 0 || delta.y != 0))
            moves.push_back({units[i].key, delta});
    }
    return moves;
}

std::vector<juce::Point<int>> computeOutputDock(const std::vector<LayoutUnit>& content,
                                                const std::vector<juce::Point<int>>& dockSizes, int dockTopY) {
    // Round UP to the grid, negative-safe (floor division on the shifted value).
    const auto up = [](int v) { return static_cast<int>(std::ceil(static_cast<double>(v) / kGridSize)) * kGridSize; };
    int left = kArrangeOriginX;
    if (!content.empty()) {
        int right = content.front().rect.getRight();
        for (const auto& unit : content)
            right = std::max(right, unit.rect.getRight());
        left = up(right) + kLayerGapX;
    }

    const int y = snap(dockTopY);
    std::vector<juce::Point<int>> positions;
    positions.reserve(dockSizes.size());
    int x = left;
    for (const auto& size : dockSizes) {
        positions.push_back({x, y});
        x = up(x + size.x + kOutputDockCardGapX);
    }
    return positions;
}

} // namespace synth::LayoutUtil
