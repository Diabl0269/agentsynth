#pragma once

#include "LanePointSelection.h"
#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// LanePointGlide -- keeps the points an automation lane editor last showed, so a change that only removes points (a
// delete, a redo) or only brings points back (an undo, a paste) can be drawn as a short animation instead of a jump:
// the points that go shrink and fade out over 110 ms, the points that come fade in over 160 ms, and the curve
// cross-fades between the old and the new shape. Any other change (a move, a stroke) lands at once.
//
// The owner must outlive it, feeds it every change of the lane's points and paints from it; it repaints the owner
// itself. A hidden owner or Reduce Motion lands at once. Message thread only.
namespace synth::ui {

class LanePointGlide {
public:
    static constexpr double kOutMs = 110.0;
    static constexpr double kInMs = 160.0;

    explicit LanePointGlide(juce::Component& owner);
    ~LanePointGlide();

    // Takes `now` as the lane's points with no animation (a new lane, a first sync).
    void reset(std::vector<LaneBreakpoint> now);
    // The lane's points are now `now`; animates when this only removed or only added points.
    void pointsChanged(std::vector<LaneBreakpoint> now);

    bool isRunning() const noexcept { return running_; }
    // Eased progress: 0 where the change started, 1 settled.
    float amount() const noexcept { return amount_; }
    // The points before the running change (curve cross-fade source).
    const std::vector<LaneBreakpoint>& before() const noexcept { return before_; }
    // How present the point at `beat` is right now: 1 for one that stays, shrinking to 0 for one leaving, growing from
    // 0 for one arriving.
    float presence(double beat) const;
    // The points leaving in the running change, still to be drawn.
    const std::vector<LaneBreakpoint>& leaving() const noexcept { return leaving_; }

private:
    bool animates() const;
    void finish();

    juce::Component& owner_;
    std::vector<LaneBreakpoint> shown_;
    std::vector<LaneBreakpoint> before_;
    std::vector<LaneBreakpoint> leaving_;
    std::vector<LaneBreakpoint> entering_;
    bool running_ = false;
    float amount_ = 1.0f;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver driver_;
};

} // namespace synth::ui
