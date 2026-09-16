#pragma once

#include "CurveModel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

namespace synth::ui {

/** Hit-test result kind for `CurveEditorGeometry::hitTest`. */
enum class CurveHitKind { None, Node, BendHandle };

struct CurveHitResult {
    CurveHitKind kind = CurveHitKind::None;
    int index = -1;
};

/** A point along the curve the transport is currently at: `segment` + a progress in [0, 1]
 *  through it, the same shape `CurveModel::valueAt` takes. */
struct CurvePlayhead {
    int segment = 0;
    float progress = 0.0f;
};

inline bool operator==(const CurvePlayhead& a, const CurvePlayhead& b) {
    return a.segment == b.segment && a.progress == b.progress;
}

/** Tunables for `CurveEditorGeometry`'s x mapping. */
struct CurveGeometryConfig {
    /** Floor under the visible time range (model units); the actual visible range is
     *  `max(totalDuration * 1.1, minVisibleRange)`, unless `explicitVisibleRange` is set. */
    double minVisibleRange = 0.0;
    /** Overrides the computed visible range entirely when set. */
    std::optional<double> explicitVisibleRange;
};

/** Pure pixel mapping for a `CurveModel` over a pixel rectangle — testable without a
 *  `juce::Component`. Constructed fresh whenever the model or bounds change (construction is
 *  O(numSegments), cheap enough to redo per paint/hit-test/drag).
 *
 *  x mapping is piecewise per segment: a zero-duration segment always occupies exactly
 *  `kZeroSegmentPx` on screen (the model value stays 0 — this is a display convention only);
 *  the remaining width is shared among the real-duration segments in proportion to
 *  `duration / visibleRange`, where `visibleRange >= totalDuration` leaves headroom on the
 *  right of the last node to drag it further out.
 */
class CurveEditorGeometry {
public:
    static constexpr float kZeroSegmentPx = 12.0f;
    static constexpr float kHitRadiusPx = 10.0f;
    /** Vertical inset so a node at level 0 or 1 isn't clipped against the component edge. */
    static constexpr float kLevelInsetPx = 10.0f;

    CurveEditorGeometry(const CurveModel& model, juce::Rectangle<float> bounds, CurveGeometryConfig config = {});

    double getVisibleRange() const noexcept { return visibleRange_; }

    // ---------- x <-> time (piecewise across segments) ----------
    float xForTime(double time) const;
    /** Inverse of `xForTime`. A point inside a zero-duration segment's plateau resolves to that
     *  segment's start time (there is no meaningful "progress" across a single time instant). */
    double timeForX(float x) const;

    // ---------- y <-> level ----------
    float yForLevel(float level) const;
    float levelForY(float y) const;

    juce::Point<float> nodePosition(int index) const;

    /** Bend-handle position for `segment`: the point ON THE CURVE at progress 0.5, so it tracks
     *  the curve as bend changes. `nullopt` (no handle drawn/hittable) for a zero-duration
     *  segment, a non-bendable segment, or one whose start/end levels are equal (flat — bend
     *  has no visible effect). */
    std::optional<juce::Point<float>> bendHandlePosition(int segment) const;

    juce::Point<float> playheadPosition(const CurvePlayhead& playhead) const;

    /** Nearest node within `kHitRadiusPx`, else nearest bend handle within radius, else None.
     *  Nodes take priority over handles. On an exact pixel tie, the earlier index wins. A node
     *  with neither axis movable (fully pinned) is never hit. */
    CurveHitResult hitTest(juce::Point<float> point) const;

    /** A "nice" (1/2/5 x 10^n) time-axis tick step targeting ~60-100px spacing, as tick times
     *  from 0 to the visible range. */
    std::vector<double> computeGridTicks() const;

    /** Default tick-label formatter: "0", "250ms", "1s", "1.5s" style. Callers with different
     *  x units (e.g. an LFO's phase) supply their own formatter instead. */
    static juce::String defaultTimeLabel(double seconds);

private:
    void buildSegmentPixelSpans();
    int segmentForTime(double time) const;
    int segmentForX(float x) const;

    const CurveModel& model_;
    juce::Rectangle<float> bounds_;
    CurveGeometryConfig config_;

    double visibleRange_ = 1.0;
    // nodeXPx_[i] is node i's pixel x; size is numNodes. A trailing "tail" beyond the last node
    // (the drag headroom) is handled separately in xForTime/timeForX.
    std::vector<float> nodeXPx_;
};

} // namespace synth::ui
