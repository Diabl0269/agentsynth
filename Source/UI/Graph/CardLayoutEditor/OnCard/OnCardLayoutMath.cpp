// OnCardLayoutMath.cpp -- snapping a dragged control to its neighbours' lines and pushing crowded
// neighbours aside. Pure: rectangles in, rectangles out.
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

} // namespace synth::ui::oncard
