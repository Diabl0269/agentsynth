#include "CurveEditorGeometry.h"
#include <cmath>

namespace synth::ui {

CurveEditorGeometry::CurveEditorGeometry(const CurveModel& model, juce::Rectangle<float> bounds,
                                         CurveGeometryConfig config)
    : model_(model)
    , bounds_(bounds)
    , config_(config) {
    buildSegmentPixelSpans();
}

void CurveEditorGeometry::buildSegmentPixelSpans() {
    const int numSegments = model_.getNumSegments();
    nodeXPx_.assign((size_t)model_.getNumNodes(), bounds_.getX());
    if (numSegments <= 0)
        return;

    double totalDuration = 0.0;
    int zeroCount = 0;
    for (int seg = 0; seg < numSegments; ++seg) {
        const double d = model_.segmentDuration(seg);
        totalDuration += d;
        if (d <= 0.0)
            ++zeroCount;
    }

    visibleRange_ = config_.explicitVisibleRange.has_value() ? *config_.explicitVisibleRange
                                                             : juce::jmax(totalDuration * 1.1, config_.minVisibleRange);
    if (visibleRange_ <= 0.0)
        visibleRange_ = 1.0;

    const float zeroPx = kZeroSegmentPx * (float)zeroCount;
    const float nonZeroPx = juce::jmax(0.0f, bounds_.getWidth() - zeroPx);

    float x = bounds_.getX();
    nodeXPx_[0] = x;
    for (int seg = 0; seg < numSegments; ++seg) {
        const double d = model_.segmentDuration(seg);
        const float width = (d <= 0.0) ? kZeroSegmentPx : (float)(nonZeroPx * (d / visibleRange_));
        x += width;
        nodeXPx_[(size_t)seg + 1] = x;
    }
}

int CurveEditorGeometry::segmentForTime(double time) const {
    const int numSegments = model_.getNumSegments();
    for (int seg = 0; seg < numSegments; ++seg)
        if (time <= model_.getNode(seg + 1).x || seg == numSegments - 1)
            return seg;
    return -1;
}

int CurveEditorGeometry::segmentForX(float x) const {
    const int numSegments = model_.getNumSegments();
    for (int seg = 0; seg < numSegments; ++seg)
        if (x <= nodeXPx_[(size_t)seg + 1] || seg == numSegments - 1)
            return seg;
    return -1;
}

float CurveEditorGeometry::xForTime(double time) const {
    const int numSegments = model_.getNumSegments();
    if (numSegments <= 0)
        return bounds_.getX();

    const double lastNodeTime = model_.getMaxX();
    if (time > lastNodeTime) {
        // Tail beyond the last node: the drag-headroom zone implied by visibleRange_ having
        // headroom over the real total duration. Extrapolate linearly so a caller probing just
        // past the last node (e.g. clamping a drag) gets a continuous mapping.
        const double totalDuration = lastNodeTime - model_.getMinX();
        const double tailDuration = visibleRange_ - totalDuration;
        const float tailPx = bounds_.getRight() - nodeXPx_.back();
        if (tailDuration <= 0.0)
            return bounds_.getRight();
        const double frac = (time - lastNodeTime) / tailDuration;
        return nodeXPx_.back() + tailPx * (float)frac;
    }

    const int seg = segmentForTime(time);
    if (seg < 0)
        return bounds_.getX();

    const double segStartTime = model_.getNode(seg).x;
    const double segEndTime = model_.getNode(seg + 1).x;
    const float segStartPx = nodeXPx_[(size_t)seg];
    const float segEndPx = nodeXPx_[(size_t)seg + 1];
    if (segEndTime <= segStartTime)
        return segStartPx; // zero-duration plateau: no meaningful interior position

    const double frac = (time - segStartTime) / (segEndTime - segStartTime);
    return segStartPx + (segEndPx - segStartPx) * (float)frac;
}

double CurveEditorGeometry::timeForX(float x) const {
    const int numSegments = model_.getNumSegments();
    if (numSegments <= 0)
        return model_.getMinX();

    if (x > nodeXPx_.back()) {
        const double totalDuration = model_.getMaxX() - model_.getMinX();
        const double tailDuration = visibleRange_ - totalDuration;
        const float tailPx = bounds_.getRight() - nodeXPx_.back();
        if (tailPx <= 0.0f || tailDuration <= 0.0)
            return model_.getMaxX();
        const float frac = (x - nodeXPx_.back()) / tailPx;
        return model_.getMaxX() + tailDuration * frac;
    }

    const int seg = segmentForX(x);
    if (seg < 0)
        return model_.getMinX();

    const double segStartTime = model_.getNode(seg).x;
    const double segEndTime = model_.getNode(seg + 1).x;
    const float segStartPx = nodeXPx_[(size_t)seg];
    const float segEndPx = nodeXPx_[(size_t)seg + 1];
    if (segEndPx <= segStartPx || segEndTime <= segStartTime)
        return segStartTime; // zero-width plateau -> segment start time

    const float frac = (x - segStartPx) / (segEndPx - segStartPx);
    return segStartTime + (segEndTime - segStartTime) * (double)frac;
}

float CurveEditorGeometry::yForLevel(float level) const {
    const float usableHeight = juce::jmax(1.0f, bounds_.getHeight() - 2.0f * kLevelInsetPx);
    return bounds_.getBottom() - kLevelInsetPx - level * usableHeight;
}

float CurveEditorGeometry::levelForY(float y) const {
    const float usableHeight = juce::jmax(1.0f, bounds_.getHeight() - 2.0f * kLevelInsetPx);
    return juce::jlimit(0.0f, 1.0f, (bounds_.getBottom() - kLevelInsetPx - y) / usableHeight);
}

juce::Point<float> CurveEditorGeometry::nodePosition(int index) const {
    return {nodeXPx_.at((size_t)index), yForLevel(model_.getNode(index).y)};
}

std::optional<juce::Point<float>> CurveEditorGeometry::bendHandlePosition(int segment) const {
    if (segment < 0 || segment >= model_.getNumSegments())
        return std::nullopt;
    if (!model_.isBendable(segment))
        return std::nullopt;
    if (model_.segmentDuration(segment) <= 0.0)
        return std::nullopt;
    if (model_.getNode(segment).y == model_.getNode(segment + 1).y)
        return std::nullopt;

    const float midX = (nodeXPx_[(size_t)segment] + nodeXPx_[(size_t)segment + 1]) * 0.5f;
    const float midLevel = model_.valueAt(segment, 0.5f);
    return juce::Point<float>{midX, yForLevel(midLevel)};
}

juce::Point<float> CurveEditorGeometry::playheadPosition(const CurvePlayhead& playhead) const {
    const int numSegments = model_.getNumSegments();
    if (numSegments <= 0)
        return {bounds_.getX(), yForLevel(0.0f)};

    const int seg = juce::jlimit(0, numSegments - 1, playhead.segment);
    const float progress = juce::jlimit(0.0f, 1.0f, playhead.progress);
    const float x = nodeXPx_[(size_t)seg] + (nodeXPx_[(size_t)seg + 1] - nodeXPx_[(size_t)seg]) * progress;
    const float level = model_.valueAt(seg, progress);
    return {x, yForLevel(level)};
}

CurveHitResult CurveEditorGeometry::hitTest(juce::Point<float> point) const {
    constexpr float radiusSq = kHitRadiusPx * kHitRadiusPx;

    int bestNode = -1;
    float bestNodeDistSq = 0.0f;
    for (int i = 0; i < model_.getNumNodes(); ++i) {
        const auto& node = model_.getNode(i);
        if (!node.xMovable && !node.yMovable)
            continue; // fully pinned -- not hittable
        const float distSq = nodePosition(i).getDistanceSquaredFrom(point);
        if (distSq > radiusSq)
            continue;
        if (bestNode == -1 || distSq < bestNodeDistSq) {
            bestNode = i;
            bestNodeDistSq = distSq;
        }
    }
    if (bestNode != -1)
        return {CurveHitKind::Node, bestNode};

    int bestSegment = -1;
    float bestSegmentDistSq = 0.0f;
    for (int seg = 0; seg < model_.getNumSegments(); ++seg) {
        const auto handle = bendHandlePosition(seg);
        if (!handle.has_value())
            continue;
        const float distSq = handle->getDistanceSquaredFrom(point);
        if (distSq > radiusSq)
            continue;
        if (bestSegment == -1 || distSq < bestSegmentDistSq) {
            bestSegment = seg;
            bestSegmentDistSq = distSq;
        }
    }
    if (bestSegment != -1)
        return {CurveHitKind::BendHandle, bestSegment};

    return {CurveHitKind::None, -1};
}

std::vector<double> CurveEditorGeometry::computeGridTicks() const {
    std::vector<double> ticks;
    if (visibleRange_ <= 0.0 || bounds_.getWidth() <= 0.0f)
        return ticks;

    constexpr float kTargetPxPerTick = 80.0f;
    const int approxCount = juce::jmax(1, (int)(bounds_.getWidth() / kTargetPxPerTick));
    const double rawStep = visibleRange_ / (double)approxCount;

    const double exponent = std::floor(std::log10(rawStep));
    const double magnitude = std::pow(10.0, exponent);
    const double fraction = rawStep / magnitude;
    const double niceFraction = (fraction <= 1.0) ? 1.0 : (fraction <= 2.0) ? 2.0 : (fraction <= 5.0) ? 5.0 : 10.0;
    const double step = niceFraction * magnitude;

    for (double t = 0.0; t <= visibleRange_ + step * 1.0e-6; t += step)
        ticks.push_back(t);
    return ticks;
}

juce::String CurveEditorGeometry::defaultTimeLabel(double seconds) {
    if (std::abs(seconds) < 1.0e-9)
        return "0";
    if (std::abs(seconds) < 1.0) {
        const int ms = (int)std::round(seconds * 1000.0);
        return juce::String(ms) + "ms";
    }
    juce::String text(seconds, 3);
    while (text.endsWithChar('0'))
        text = text.dropLastCharacters(1);
    if (text.endsWithChar('.'))
        text = text.dropLastCharacters(1);
    return text + "s";
}

} // namespace synth::ui
