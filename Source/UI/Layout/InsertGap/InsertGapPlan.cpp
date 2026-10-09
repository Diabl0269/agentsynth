// InsertGapPlan.cpp -- the pure insert-between geometry; see InsertGapPlan.h and
// docs/layout/layout.md#making-room-for-a-module-dropped-between-others.

#include "InsertGapPlan.h"

#include <algorithm>
#include <limits>
#include <map>

namespace synth::insert_gap {

namespace {
using LayoutUtil::LayoutUnit;
using LayoutUtil::UnitMove;
using Rect = juce::Rectangle<int>;

// Every helper below reads a rectangle along the axis (where a row runs) or across it, so one implementation serves
// both a row and a column.
int lead(const Rect& r, Axis a) { return a == Axis::Row ? r.getX() : r.getY(); }
int trail(const Rect& r, Axis a) { return a == Axis::Row ? r.getRight() : r.getBottom(); }
int extent(juce::Point<int> size, Axis a) { return a == Axis::Row ? size.x : size.y; }
float centreAlong(const Rect& r, Axis a) {
    return a == Axis::Row ? r.toFloat().getCentreX() : r.toFloat().getCentreY();
}
bool overlapsAcross(const Rect& a, const Rect& b, Axis axis) {
    return axis == Axis::Row ? (a.getY() < b.getBottom() && b.getY() < a.getBottom())
                             : (a.getX() < b.getRight() && b.getX() < a.getRight());
}
juce::Point<int> along(int d, Axis a) { return a == Axis::Row ? juce::Point<int>(d, 0) : juce::Point<int>(0, d); }
Rect moved(const Rect& r, int d, Axis a) { return r + along(d, a); }

const LayoutUnit* findUnit(const std::vector<LayoutUnit>& units, const juce::String& key) {
    const auto it = std::find_if(units.begin(), units.end(), [&key](const LayoutUnit& u) { return u.key == key; });
    return it != units.end() ? &*it : nullptr;
}

// Distance from `p` to the nearest point of `r`, per axis (0 inside the rect's span).
int outside(int p, int lo, int hi) { return p < lo ? lo - p : (p >= hi ? p - hi + 1 : 0); }

// Something that pushes in the cascade: the slot the new card takes (it has no "before"), a grown unit, or a unit the
// cascade already moved.
struct Pusher {
    juce::String key; // empty for the slot / grown unit
    Rect before, after;
};

// Pushes every unit (other than pinned ones) that a pusher now covers, with the usual clearance, along `axis`, as far
// as it takes to clear it, and repeats with the pushed units as pushers until nothing moves. Only units AHEAD of the
// pusher (their leading edge at or past the pusher's own) are pushed, so nothing is ever flipped to the other side, and
// a unit that already overlapped the pusher before anything moved is left as it was. Deltas only grow, so it settles.
void cascade(const std::vector<LayoutUnit>& units, std::map<juce::String, int>& delta, const std::vector<Pusher>& fixed,
             Axis axis) {
    const int gap = LayoutUtil::kCollisionGap;
    const auto rounds = static_cast<size_t>(LayoutUtil::kDisplacementMaxRounds) * (units.size() + 1);
    for (size_t round = 0; round < rounds; ++round) {
        std::vector<Pusher> pushers = fixed;
        for (const auto& u : units)
            if (const auto d = delta.find(u.key); d != delta.end() && d->second > 0)
                pushers.push_back({u.key, u.rect, moved(u.rect, d->second, axis)});
        bool changed = false;
        for (const auto& q : units) {
            if (q.pinned)
                continue;
            for (const auto& p : pushers) {
                if (p.key == q.key)
                    continue;
                const auto qNow = moved(q.rect, delta[q.key], axis);
                if (!p.after.expanded(gap).intersects(qNow))
                    continue;
                if (!p.before.isEmpty() && p.before.intersects(q.rect))
                    continue; // they overlapped before anything moved: not this push's doing
                const int from = p.before.isEmpty() ? lead(p.after, axis) : lead(p.before, axis);
                if (lead(q.rect, axis) < from)
                    continue;
                // A unit that already sat closer than the clearance keeps that distance rather than being shoved to
                // the full gap, so a tight row moves as one and keeps its spacing.
                const int keep =
                    p.before.isEmpty() ? gap : juce::jlimit(0, gap, lead(q.rect, axis) - trail(p.before, axis));
                const int need = snapUp(trail(p.after, axis) + keep - lead(qNow, axis));
                if (need > 0) {
                    delta[q.key] += need;
                    changed = true;
                }
            }
        }
        if (!changed)
            break;
    }
}

std::vector<UnitMove> movesFrom(const std::vector<LayoutUnit>& units, const std::map<juce::String, int>& delta,
                                Axis axis) {
    std::vector<const LayoutUnit*> movedUnits;
    for (const auto& u : units)
        if (const auto d = delta.find(u.key); d != delta.end() && d->second > 0)
            movedUnits.push_back(&u);
    std::stable_sort(movedUnits.begin(), movedUnits.end(), [axis](const LayoutUnit* a, const LayoutUnit* b) {
        return lead(a->rect, axis) < lead(b->rect, axis);
    });
    std::vector<UnitMove> out;
    for (const auto* u : movedUnits)
        out.push_back({u->key, along(delta.at(u->key), axis)});
    return out;
}

// The innermost (smallest) unit under the pointer.
const LayoutUnit* unitUnder(const std::vector<LayoutUnit>& units, juce::Point<int> p) {
    const LayoutUnit* best = nullptr;
    for (const auto& u : units)
        if (u.rect.contains(p) && (best == nullptr || u.rect.getWidth() * (int64_t)u.rect.getHeight() <
                                                          best->rect.getWidth() * (int64_t)best->rect.getHeight()))
            best = &u;
    return best;
}

// The unit the ghost touches (with clearance) that is nearest the pointer.
const LayoutUnit* nearestTouched(const std::vector<LayoutUnit>& units, juce::Point<int> p, const Rect& ghost) {
    const LayoutUnit* best = nullptr;
    int64_t bestDist = std::numeric_limits<int64_t>::max();
    for (const auto& u : units) {
        if (!ghost.expanded(LayoutUtil::kCollisionGap).intersects(u.rect))
            continue;
        const int64_t dx = outside(p.x, u.rect.getX(), u.rect.getRight());
        const int64_t dy = outside(p.y, u.rect.getY(), u.rect.getBottom());
        if (dx * dx + dy * dy < bestDist) {
            bestDist = dx * dx + dy * dy;
            best = &u;
        }
    }
    return best;
}
} // namespace

int snapUp(int v) noexcept {
    if (v <= 0)
        return 0;
    return ((v + LayoutUtil::kGridSize - 1) / LayoutUtil::kGridSize) * LayoutUtil::kGridSize;
}

std::optional<juce::String> nextAfter(const std::vector<LayoutUnit>& units, const juce::String& unitKey, Axis axis) {
    const auto* self = findUnit(units, unitKey);
    if (self == nullptr)
        return std::nullopt;
    const LayoutUnit* best = nullptr;
    for (const auto& u : units) {
        if (u.key == unitKey || !overlapsAcross(u.rect, self->rect, axis) ||
            centreAlong(u.rect, axis) <= centreAlong(self->rect, axis))
            continue;
        if (best == nullptr || lead(u.rect, axis) < lead(best->rect, axis))
            best = &u;
    }
    return best != nullptr ? std::optional<juce::String>(best->key) : std::nullopt;
}

// Inside a card, only its edge bands count (the nearer edge, in pixels, picks the axis): deeper in is an ordinary drop
// onto the card. In empty space the ghost has to touch a card; the nearest one decides: beside it is a row, above or
// below it a column, and off a corner the farther offset wins. The leading half of the card means "in front of it",
// the trailing half "in front of the next one".
std::optional<Target> pickTarget(const std::vector<LayoutUnit>& units, juce::Point<int> pointer,
                                 juce::Point<int> size) {
    const LayoutUnit* ref = unitUnder(units, pointer);
    Axis axis = Axis::Row;
    if (ref != nullptr) {
        const auto& r = ref->rect;
        const int ex = std::min(pointer.x - r.getX(), r.getRight() - pointer.x);
        const int ey = std::min(pointer.y - r.getY(), r.getBottom() - pointer.y);
        const bool rowBand = ex <= static_cast<int>(static_cast<float>(r.getWidth()) * kEdgeBand);
        const bool columnBand = ey <= static_cast<int>(static_cast<float>(r.getHeight()) * kEdgeBand);
        if (!rowBand && !columnBand)
            return std::nullopt;
        axis = rowBand && (!columnBand || ex <= ey) ? Axis::Row : Axis::Column;
    } else {
        const Rect ghost(pointer.x - size.x / 2, pointer.y - size.y / 2, size.x, size.y);
        ref = nearestTouched(units, pointer, ghost);
        if (ref == nullptr)
            return std::nullopt;
        const auto& r = ref->rect;
        const int dx = outside(pointer.x, r.getX(), r.getRight());
        const int dy = outside(pointer.y, r.getY(), r.getBottom());
        axis = dy == 0 ? Axis::Row : (dx == 0 ? Axis::Column : (dx >= dy ? Axis::Row : Axis::Column));
    }
    const bool leading = axis == Axis::Row ? static_cast<float>(pointer.x) < centreAlong(ref->rect, axis)
                                           : static_cast<float>(pointer.y) < centreAlong(ref->rect, axis);
    const auto anchor = leading ? std::optional<juce::String>(ref->key) : nextAfter(units, ref->key, axis);
    if (!anchor.has_value())
        return std::nullopt;
    const auto* anchorUnit = findUnit(units, *anchor);
    if (anchorUnit == nullptr || anchorUnit->pinned)
        return std::nullopt;
    return Target{axis, *anchor};
}

// The card copies the spacing between the anchor and the unit before it in the row (or, at the start of a row, the
// spacing after the anchor), held between the collision gap and kMaxSpacing, and takes the anchor's place: right of
// the unit before it by that spacing, snapped up to the grid, level with the anchor. The anchor and every unit of its
// row from the anchor on move by one shared amount, so their spacing is kept, and the cascade carries on from there.
std::optional<Plan> planBefore(const std::vector<LayoutUnit>& units, const Target& target, juce::Point<int> size) {
    const auto* anchor = findUnit(units, target.anchorKey);
    if (anchor == nullptr || anchor->pinned)
        return std::nullopt;
    const Axis axis = target.axis;
    const auto& b = anchor->rect;

    const LayoutUnit* before = nullptr;
    const LayoutUnit* after = nullptr;
    for (const auto& u : units) {
        if (u.key == anchor->key || !overlapsAcross(u.rect, b, axis))
            continue;
        if (trail(u.rect, axis) <= lead(b, axis) &&
            (before == nullptr || trail(u.rect, axis) > trail(before->rect, axis)))
            before = &u;
        if (lead(u.rect, axis) >= trail(b, axis) && (after == nullptr || lead(u.rect, axis) < lead(after->rect, axis)))
            after = &u;
    }
    int spacing = before != nullptr  ? lead(b, axis) - trail(before->rect, axis)
                  : after != nullptr ? lead(after->rect, axis) - trail(b, axis)
                                     : kDefaultSpacing;
    spacing = juce::jlimit(LayoutUtil::kCollisionGap, kMaxSpacing, spacing);

    Plan plan;
    plan.target = target;
    plan.spacing = spacing;
    const int slotLead = before != nullptr ? snapUp(trail(before->rect, axis) + spacing) : lead(b, axis);
    plan.slot = axis == Axis::Row ? Rect(slotLead, b.getY(), size.x, size.y) : Rect(b.getX(), slotLead, size.x, size.y);

    std::map<juce::String, int> delta;
    const int shift = snapUp(slotLead + extent(size, axis) + spacing - lead(b, axis));
    if (shift > 0)
        for (const auto& u : units)
            if (!u.pinned &&
                (u.key == anchor->key || (overlapsAcross(u.rect, b, axis) && lead(u.rect, axis) >= lead(b, axis))))
                delta[u.key] = shift;
    cascade(units, delta, {Pusher{{}, {}, plan.slot}}, axis);
    plan.moves = movesFrom(units, delta, axis);
    return plan;
}

std::optional<Plan> planAfter(const std::vector<LayoutUnit>& units, const juce::String& afterKey,
                              juce::Point<int> size) {
    const auto next = nextAfter(units, afterKey, Axis::Row);
    if (!next.has_value())
        return std::nullopt;
    return planBefore(units, Target{Axis::Row, *next}, size);
}

std::vector<UnitMove> pushAhead(const std::vector<LayoutUnit>& units, Rect before, Rect after, Axis axis) {
    std::map<juce::String, int> delta;
    if (before == after || after.isEmpty())
        return {};
    cascade(units, delta, {Pusher{{}, before, after}}, axis);
    return movesFrom(units, delta, axis);
}

} // namespace synth::insert_gap
