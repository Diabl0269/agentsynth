#include "LanePointStretch.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace synth::ui {

namespace {
constexpr float kPadPx = 8.0f;         // the box sits this far outside the points, so a handle never covers one
constexpr float kHandleSizePx = 8.0f;  // the handle squares
constexpr float kHandleReachPx = 6.0f; // how far from a handle's centre a press still grabs it

double smallestGap(const std::vector<LaneBreakpoint>& points) {
    double gap = std::numeric_limits<double>::max();
    for (std::size_t i = 1; i < points.size(); ++i)
        gap = std::min(gap, points[i].beat - points[i - 1].beat);
    return gap;
}

bool containsBeat(const std::vector<LaneBreakpoint>& sortedPoints, double beat) {
    return std::binary_search(sortedPoints.begin(), sortedPoints.end(), LaneBreakpoint{beat, 0.0, 0.0f, 0},
                              [](const auto& a, const auto& b) { return a.beat < b.beat; });
}

// The unselected points of `all` past the dragged edge, nearest first.
std::vector<LaneBreakpoint> pointsBeyond(const std::vector<LaneBreakpoint>& selected,
                                         const std::vector<LaneBreakpoint>& all, bool right) {
    std::vector<LaneBreakpoint> beyond;
    for (const auto& p : all)
        if (!containsBeat(selected, p.beat) && (right ? p.beat > selected.back().beat : p.beat < selected.front().beat))
            beyond.push_back(p);
    if (!right)
        std::reverse(beyond.begin(), beyond.end());
    return beyond;
}
} // namespace

//==============================================================================
// Distances are measured outward from the dragged edge: growth is positive. Growing is limited on the left by beat 0
// (the pushed block must still fit) and shrinking by the closest allowed gap; the right side has no limit.
StretchResult stretchBeats(const std::vector<LaneBreakpoint>& selected, const std::vector<LaneBreakpoint>& all,
                           StretchHandle edge, double newEdgeBeat, double pushGap) {
    StretchResult result;
    result.moved = selected;
    const bool right = edge == StretchHandle::Right;
    if (selected.size() < 2 || (!right && edge != StretchHandle::Left))
        return result;
    const double first = selected.front().beat;
    const double last = selected.back().beat;
    const double span = last - first;
    result.edgeBeat = right ? last : first;
    if (span <= 0.0)
        return result;

    const double tight = smallestGap(selected);
    const double minSpan = span * std::min(kStretchMinGapBeats, tight) / tight;
    const auto beyond = pointsBeyond(selected, all, right);
    const double distanceToNext = beyond.empty() ? 0.0 : std::abs(beyond.front().beat - result.edgeBeat);

    double growLimit = std::numeric_limits<double>::max();
    if (!right)
        growLimit = beyond.empty() ? first : std::max(0.0, distanceToNext - pushGap + beyond.back().beat);
    const double wanted = right ? newEdgeBeat - last : first - newEdgeBeat;
    const double grow = juce::jlimit(-(span - minSpan), std::max(0.0, growLimit), wanted);
    result.edgeBeat = grow == wanted ? newEdgeBeat : (right ? last + grow : first - grow);

    const double scale = (span + grow) / span;
    for (auto& p : result.moved)
        p.beat = right ? first + (p.beat - first) * scale : last - (last - p.beat) * scale;
    (right ? result.moved.back() : result.moved.front()).beat = result.edgeBeat;

    const double push = grow > 0.0 && !beyond.empty() ? std::max(0.0, grow - distanceToNext + pushGap) : 0.0;
    if (push > 0.0)
        for (const auto& p : beyond) {
            result.pushedFrom.push_back(p.beat);
            auto moved = p;
            moved.beat = right ? p.beat + push : std::max(0.0, p.beat - push);
            result.pushed.push_back(moved);
        }
    return result;
}

std::vector<LaneBreakpoint> scaleValues(const std::vector<LaneBreakpoint>& selected, StretchHandle edge,
                                        double newEdgeValue, double minValue, double maxValue) {
    std::vector<LaneBreakpoint> scaled = selected;
    if (selected.empty() || (edge != StretchHandle::Top && edge != StretchHandle::Bottom))
        return scaled;
    double lowest = selected.front().value, highest = lowest;
    for (const auto& p : selected) {
        lowest = std::min(lowest, p.value);
        highest = std::max(highest, p.value);
    }
    if (highest - lowest <= 1e-12)
        return scaled;

    const bool top = edge == StretchHandle::Top;
    const double anchor = top ? lowest : highest;
    const double oldEdge = top ? highest : lowest;
    const double clamped = juce::jlimit(minValue, maxValue, newEdgeValue);
    const double newEdge = top ? std::max(clamped, anchor) : std::min(clamped, anchor);
    const double scale = (newEdge - anchor) / (oldEdge - anchor);
    for (auto& p : scaled)
        p.value = p.value == oldEdge ? newEdge : juce::jlimit(minValue, maxValue, anchor + (p.value - anchor) * scale);
    return scaled;
}

bool stretchChanged(const std::vector<LaneBreakpoint>& original, const StretchResult& result) {
    if (!result.pushed.empty())
        return true;
    for (std::size_t i = 0; i < original.size() && i < result.moved.size(); ++i)
        if (original[i].beat != result.moved[i].beat || original[i].value != result.moved[i].value)
            return true;
    return false;
}

std::vector<double> stretchRemoveBeats(const std::vector<LaneBreakpoint>& original, const StretchResult& result) {
    std::vector<double> beats;
    for (const auto& p : original)
        beats.push_back(p.beat);
    beats.insert(beats.end(), result.pushedFrom.begin(), result.pushedFrom.end());
    return beats;
}

std::vector<LaneBreakpoint> stretchAddPoints(const StretchResult& result) {
    std::vector<LaneBreakpoint> points = result.moved;
    points.insert(points.end(), result.pushed.begin(), result.pushed.end());
    return points;
}

//==============================================================================
juce::Rectangle<float> stretchBoxAround(juce::Rectangle<float> pointsBox) { return pointsBox.expanded(kPadPx); }

juce::Rectangle<float> stretchHandleRect(StretchHandle handle, juce::Rectangle<float> box,
                                         juce::Rectangle<float> bounds) {
    juce::Point<float> centre;
    switch (handle) {
    case StretchHandle::Left:
        centre = {box.getX(), box.getCentreY()};
        break;
    case StretchHandle::Right:
        centre = {box.getRight(), box.getCentreY()};
        break;
    case StretchHandle::Top:
        centre = {box.getCentreX(), box.getY()};
        break;
    case StretchHandle::Bottom:
        centre = {box.getCentreX(), box.getBottom()};
        break;
    case StretchHandle::None:
        return {};
    }
    const float half = kHandleSizePx / 2.0f;
    const auto inner = bounds.reduced(half);
    centre = {juce::jlimit(inner.getX(), std::max(inner.getX(), inner.getRight()), centre.x),
              juce::jlimit(inner.getY(), std::max(inner.getY(), inner.getBottom()), centre.y)};
    return juce::Rectangle<float>(kHandleSizePx, kHandleSizePx).withCentre(centre);
}

StretchHandle hitStretchHandle(juce::Point<float> pos, juce::Rectangle<float> box, juce::Rectangle<float> bounds) {
    if (box.isEmpty())
        return StretchHandle::None;
    StretchHandle best = StretchHandle::None;
    float bestDistance = std::numeric_limits<float>::max();
    for (auto handle : {StretchHandle::Right, StretchHandle::Left, StretchHandle::Top, StretchHandle::Bottom}) {
        const auto centre = stretchHandleRect(handle, box, bounds).getCentre();
        if (std::abs(pos.x - centre.x) > kHandleReachPx || std::abs(pos.y - centre.y) > kHandleReachPx)
            continue;
        const float distance = pos.getDistanceSquaredFrom(centre);
        if (distance < bestDistance) {
            best = handle;
            bestDistance = distance;
        }
    }
    return best;
}

juce::Rectangle<float> pointsBounds(const std::vector<LaneBreakpoint>& points, const LanePointMapper& map) {
    if (points.empty())
        return {};
    const auto first = map(points.front().beat, points.front().value);
    float left = first.x, right = first.x, top = first.y, bottom = first.y;
    for (const auto& p : points) {
        const auto at = map(p.beat, p.value);
        left = std::min(left, at.x);
        right = std::max(right, at.x);
        top = std::min(top, at.y);
        bottom = std::max(bottom, at.y);
    }
    return juce::Rectangle<float>::leftTopRightBottom(left, top, right, bottom);
}

//==============================================================================
LanePointStretch::LanePointStretch(juce::Component& owner)
    : owner_(owner)
    , vblank_(&owner) {}

LanePointStretch::~LanePointStretch() { driver_.stop(vblank_); }

bool LanePointStretch::animates() const { return owner_.isShowing() && !prefersReducedMotion(); }

void LanePointStretch::setBoxShown(bool shown) {
    if (shown == shown_)
        return;
    shown_ = shown;
    fadeTo(shown ? 1.0f : 0.0f);
}

// A fade that reverses midway starts from where the box is, so it never jumps.
void LanePointStretch::fadeTo(float target) {
    const float from = alpha_;
    if (!animates()) {
        driver_.stop(vblank_);
        alpha_ = target;
        owner_.repaint();
        return;
    }
    if (from == target)
        return;
    const bool in = target > from;
    driver_.start(
        vblank_, in ? kInMs : kOutMs, in ? easeOutCubic : easeInCubic,
        [this, from, target](float e) {
            alpha_ = from + (target - from) * e;
            owner_.repaint();
        },
        [this, target] {
            alpha_ = target;
            owner_.repaint();
        });
}

void LanePointStretch::paintBox(juce::Graphics& g, juce::Rectangle<float> liveBox, juce::Rectangle<float> bounds,
                                StretchHandle hovered) const {
    if (!liveBox.isEmpty())
        lastBox_ = liveBox;
    if (alpha_ <= 0.0f || lastBox_.isEmpty())
        return;
    juce::Colour accent = juce::Colours::yellow;
    juce::Colour outline = juce::Colours::black;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&owner_.getLookAndFeel())) {
        accent = lf->getTheme().colors.accent;
        outline = lf->getTheme().colors.bg1;
    }
    g.setColour(accent.withMultipliedAlpha(0.8f * alpha_));
    g.drawRect(lastBox_, 1.0f);
    for (auto handle : {StretchHandle::Left, StretchHandle::Right, StretchHandle::Top, StretchHandle::Bottom}) {
        const auto rect = stretchHandleRect(handle, lastBox_, bounds);
        g.setColour((handle == hovered ? accent.brighter(0.4f) : accent).withMultipliedAlpha(alpha_));
        g.fillRect(rect);
        g.setColour(outline.withMultipliedAlpha(alpha_));
        g.drawRect(rect, 1.0f);
    }
}

//==============================================================================
void LanePointStretch::begin(StretchHandle handle, std::vector<LaneBreakpoint> selected,
                             std::vector<LaneBreakpoint> all, double minValue, double maxValue, double pushGap) {
    if (selected.size() < 2 || handle == StretchHandle::None)
        return;
    handle_ = handle;
    selected_ = std::move(selected);
    all_ = std::move(all);
    minValue_ = minValue;
    maxValue_ = maxValue;
    pushGap_ = pushGap;
    double lowest = selected_.front().value, highest = lowest;
    for (const auto& p : selected_) {
        lowest = std::min(lowest, p.value);
        highest = std::max(highest, p.value);
    }
    switch (handle) {
    case StretchHandle::Left:
        edgeStart_ = selected_.front().beat;
        break;
    case StretchHandle::Right:
        edgeStart_ = selected_.back().beat;
        break;
    case StretchHandle::Top:
        edgeStart_ = highest;
        break;
    default:
        edgeStart_ = lowest;
        break;
    }
    result_ = {};
    result_.moved = selected_;
}

void LanePointStretch::update(double edge) {
    if (!isActive())
        return;
    if (handle_ == StretchHandle::Left || handle_ == StretchHandle::Right) {
        result_ = stretchBeats(selected_, all_, handle_, edge, pushGap_);
    } else {
        result_ = {};
        result_.moved = scaleValues(selected_, handle_, edge, minValue_, maxValue_);
    }
}

void LanePointStretch::cancel() noexcept {
    handle_ = StretchHandle::None;
    selected_.clear();
    all_.clear();
    result_ = {};
}

} // namespace synth::ui
