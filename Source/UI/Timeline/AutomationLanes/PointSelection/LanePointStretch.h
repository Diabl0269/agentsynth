#pragma once

#include "LanePointSelection.h"
#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// LanePointStretch -- the stretch box drawn around two or more selected automation points, its four edge handles,
// and the drag that scales the selection in time (left/right) or in value (top/bottom). The math is pure, the
// gesture only previews: nothing here touches the doc.
//
// The owner must outlive it, paints the box from its own paint() and repaints itself when asked. Message thread only.
namespace synth::ui {

enum class StretchHandle { None, Left, Right, Top, Bottom };

// The closest two stretched points may come, in beats.
constexpr double kStretchMinGapBeats = 1.0 / 64.0;

struct StretchResult {
    std::vector<LaneBreakpoint> moved;  // the selected points at their new place, in the order given
    std::vector<LaneBreakpoint> pushed; // the unselected points the edge pushed, at their new beats
    std::vector<double> pushedFrom;     // their beats before the push, parallel to `pushed`
    double edgeBeat = 0.0;              // where the dragged edge landed after the limits
};

// `selected` and `all` ascending by beat, `selected` two or more points of `all`. Scales the beats of `selected`
// about the opposite edge so the dragged edge (Left or Right) lands on `newEdgeBeat`, within the limits: no beat
// below 0, no two selected points closer than kStretchMinGapBeats. Growing past an unselected point pushes it, and
// every unselected point after it, to stay `pushGap` beyond the edge; shrinking never pushes.
StretchResult stretchBeats(const std::vector<LaneBreakpoint>& selected, const std::vector<LaneBreakpoint>& all,
                           StretchHandle edge, double newEdgeBeat, double pushGap);

// `selected` with every value scaled about the opposite edge (Top keeps the lowest value, Bottom the highest) so the
// dragged edge lands on `newEdgeValue`, clamped to [minValue, maxValue]; a selection with one value is unchanged.
std::vector<LaneBreakpoint> scaleValues(const std::vector<LaneBreakpoint>& selected, StretchHandle edge,
                                        double newEdgeValue, double minValue, double maxValue);

// True when `result` puts any selected point somewhere else or pushes one.
bool stretchChanged(const std::vector<LaneBreakpoint>& original, const StretchResult& result);
// What an edit must remove / add to apply `result` to `original` (the selected points before it).
std::vector<double> stretchRemoveBeats(const std::vector<LaneBreakpoint>& original, const StretchResult& result);
std::vector<LaneBreakpoint> stretchAddPoints(const StretchResult& result);

// The box around a set of points' own box, padded; each handle's square on it, kept inside `bounds`; and the handle
// whose reach contains `pos` (the nearest when two do).
juce::Rectangle<float> stretchBoxAround(juce::Rectangle<float> pointsBox);
juce::Rectangle<float> stretchHandleRect(StretchHandle handle, juce::Rectangle<float> box,
                                         juce::Rectangle<float> bounds);
StretchHandle hitStretchHandle(juce::Point<float> pos, juce::Rectangle<float> box, juce::Rectangle<float> bounds);
// The box around `points` as the mapper places them (empty when there are none).
juce::Rectangle<float> pointsBounds(const std::vector<LaneBreakpoint>& points, const LanePointMapper& map);

class LanePointStretch {
public:
    static constexpr double kInMs = 160.0;
    static constexpr double kOutMs = 110.0;

    explicit LanePointStretch(juce::Component& owner);
    ~LanePointStretch();

    // ---- The box: fades in and out; Reduce Motion or a hidden owner lands at once ----
    void setBoxShown(bool shown);
    float boxAlpha() const noexcept { return alpha_; }
    // Draws the box and handles at the current fade; `liveBox` empty keeps the last one so a fade-out has a shape.
    void paintBox(juce::Graphics& g, juce::Rectangle<float> liveBox, juce::Rectangle<float> bounds,
                  StretchHandle hovered) const;

    // ---- The drag ----
    // `selected` and `all` as stretchBeats takes them.
    void begin(StretchHandle handle, std::vector<LaneBreakpoint> selected, std::vector<LaneBreakpoint> all,
               double minValue, double maxValue, double pushGap);
    // Moves the dragged edge to `edge` (a beat for Left/Right, a value for Top/Bottom).
    void update(double edge);
    void cancel() noexcept;
    bool isActive() const noexcept { return handle_ != StretchHandle::None; }
    StretchHandle handle() const noexcept { return handle_; }
    // Where the dragged edge stood when the drag began.
    double edgeStart() const noexcept { return edgeStart_; }
    // The stretched selection and anything it pushed; equals the original until update().
    const StretchResult& result() const noexcept { return result_; }
    const std::vector<LaneBreakpoint>& original() const noexcept { return selected_; }
    // True once the preview differs from where the points started.
    bool changed() const { return stretchChanged(selected_, result_); }

    // Tests: animate as if the owner were on screen, so no native window is needed.
    void forceAnimateForTest(bool force) noexcept { forceAnimateForTest_ = force; }

private:
    bool animates() const;
    void fadeTo(float target);

    juce::Component& owner_;
    bool shown_ = false;
    bool forceAnimateForTest_ = false;
    float alpha_ = 0.0f;
    mutable juce::Rectangle<float> lastBox_;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver driver_;

    StretchHandle handle_ = StretchHandle::None;
    std::vector<LaneBreakpoint> selected_;
    std::vector<LaneBreakpoint> all_;
    double minValue_ = 0.0;
    double maxValue_ = 1.0;
    double pushGap_ = kStretchMinGapBeats;
    double edgeStart_ = 0.0;
    StretchResult result_;
};

} // namespace synth::ui
