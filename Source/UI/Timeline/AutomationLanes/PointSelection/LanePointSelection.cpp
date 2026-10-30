#include "LanePointSelection.h"
#include "UI/Layout/PointEditing/PointGroupMove.h"
#include <algorithm>
#include <limits>
#include <set>

namespace synth::ui {

LanePointSelection::LanePointSelection(juce::Component& owner)
    : owner_(owner)
    , box_(owner) {}

void LanePointSelection::notify() {
    owner_.repaint();
    if (onChange)
        onChange();
}

void LanePointSelection::clear() {
    if (model_.clear())
        notify();
}

bool LanePointSelection::add(double beat) {
    if (!model_.add(beat))
        return false;
    notify();
    return true;
}

bool LanePointSelection::remove(double beat) {
    if (!model_.remove(beat))
        return false;
    notify();
    return true;
}

bool LanePointSelection::toggle(double beat) {
    if (contains(beat)) {
        remove(beat);
        return false;
    }
    return add(beat);
}

void LanePointSelection::setSelection(const std::vector<double>& beats) {
    if (model_.setSelected(beats))
        notify();
}

bool LanePointSelection::retainOnly(const std::vector<double>& alive) {
    const bool changed = model_.retainOnly(alive);
    if (changed)
        notify();
    return changed;
}

void LanePointSelection::setCursor(std::optional<double> beat) {
    if (model_.setCursor(beat))
        notify();
}

// Each key is one notification, however many of the set, the cursor and the anchor it changed.
std::optional<double> LanePointSelection::step(int direction, const std::vector<double>& beats) {
    const auto before = model_;
    const auto landed = model_.step(direction, beats, kRules);
    if (model_.selected() != before.selected() || model_.cursor() != before.cursor())
        notify();
    return landed;
}

std::optional<double> LanePointSelection::extend(int direction, const std::vector<double>& beats) {
    const auto before = model_;
    const auto landed = model_.extend(direction, beats, kRules);
    if (model_.selected() != before.selected() || model_.cursor() != before.cursor())
        notify();
    return landed;
}

bool LanePointSelection::selectAll(const std::vector<double>& beats) {
    const auto before = model_;
    const bool any = model_.selectAll(beats, kRules);
    if (model_.selected() != before.selected() || model_.cursor() != before.cursor())
        notify();
    return any;
}

std::vector<LaneBreakpoint> LanePointSelection::selectedPoints(const std::vector<LaneBreakpoint>& lane) const {
    std::vector<LaneBreakpoint> out;
    for (const auto& p : lane)
        if (contains(p.beat))
            out.push_back(p);
    return out;
}

// Built from the corners by hand: Rectangle::getUnion skips empty rectangles, which a single point's box is.
juce::Rectangle<float> LanePointSelection::boundingBox(const std::vector<LaneBreakpoint>& lane,
                                                       const LanePointMapper& map) const {
    const auto picked = selectedPoints(lane);
    if (picked.empty())
        return {};
    auto first = map(picked.front().beat, picked.front().value);
    float left = first.x, right = first.x, top = first.y, bottom = first.y;
    for (const auto& p : picked) {
        const auto at = map(p.beat, p.value);
        left = std::min(left, at.x);
        right = std::max(right, at.x);
        top = std::min(top, at.y);
        bottom = std::max(bottom, at.y);
    }
    return juce::Rectangle<float>::leftTopRightBottom(left, top, right, bottom);
}

//==============================================================================
// The gesture itself is the shared PointBoxSelect; the lane supplies the beats inside the box.
void LanePointSelection::pressEmpty(juce::Point<int> pos, bool additive) {
    box_.press(pos, additive, model_.selected());
}

bool LanePointSelection::dragTo(juce::Point<int> pos, const std::vector<LaneBreakpoint>& lane,
                                const LanePointMapper& map) {
    const auto next = box_.dragTo(pos, [&](juce::Rectangle<float> r) { return beatsInBox(lane, map, r); });
    if (!next)
        return false;
    setSelection(*next);
    return true;
}

bool LanePointSelection::release() {
    const auto ended = box_.release();
    if (ended == PointBoxGesture::Release::click)
        clear();
    return ended != PointBoxGesture::Release::none;
}

void LanePointSelection::cancelGesture() { box_.cancel(); }

void LanePointSelection::paintMarquee(juce::Graphics& g) const { box_.paintMarquee(g); }

//==============================================================================
// A lane has no end and its block may pass unselected points (a move onto a point's beat replaces it), so the only
// limit is beat 0.
double clampBeatDelta(const std::vector<LaneBreakpoint>& points, double delta) {
    std::vector<PointRoom> rooms;
    rooms.reserve(points.size());
    for (const auto& p : points)
        rooms.push_back(roomWithin(p.beat, 0.0, std::numeric_limits<double>::infinity()));
    return clampGroupDelta(rooms, delta);
}

std::vector<LaneBreakpoint> movePoints(const std::vector<LaneBreakpoint>& points, double beatDelta, double valueDelta,
                                       double minValue, double maxValue) {
    std::vector<LaneBreakpoint> moved = points;
    for (auto& p : moved) {
        p.beat += beatDelta;
        p.value = juce::jlimit(minValue, maxValue, p.value + valueDelta);
    }
    return moved;
}

std::vector<LaneBreakpoint> replacePoints(std::vector<LaneBreakpoint> lane, const std::vector<double>& removeBeats,
                                          const std::vector<LaneBreakpoint>& add) {
    const std::set<double> gone(removeBeats.begin(), removeBeats.end());
    lane.erase(std::remove_if(lane.begin(), lane.end(), [&](const auto& p) { return gone.count(p.beat) > 0; }),
               lane.end());
    for (const auto& p : add) {
        lane.erase(std::remove_if(lane.begin(), lane.end(), [&](const auto& q) { return q.beat == p.beat; }),
                   lane.end());
        lane.push_back(p);
    }
    std::sort(lane.begin(), lane.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
    return lane;
}

std::vector<double> beatsInBox(const std::vector<LaneBreakpoint>& lane, const LanePointMapper& map,
                               juce::Rectangle<float> box) {
    std::vector<double> beats;
    if (box.isEmpty())
        return beats;
    for (const auto& p : lane)
        if (box.contains(map(p.beat, p.value)))
            beats.push_back(p.beat);
    return beats;
}

} // namespace synth::ui
