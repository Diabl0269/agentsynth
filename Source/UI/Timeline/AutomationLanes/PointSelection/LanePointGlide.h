#pragma once

#include "LanePointSelection.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// LanePointGlide -- keeps the points an automation lane editor last showed, so a change that only removes points (a
// delete, a redo) or only brings points back (an undo, a paste) can be drawn as a short animation instead of a jump:
// the points that go shrink and fade out over 110 ms, the points that come fade in over 160 ms, and the curve
// cross-fades between the old and the new shape. Any other change (a move, a stroke) lands at once, unless the owner
// armed a melt (meltNext): then the next change of points animates whatever it did, as the old curve melting into the
// new one inside a beat range over motionMs(200, 80) (a short fade under Reduce Motion, at once under Animations Off),
// repainting only that range.
//
// The owner must outlive it, feeds it every change of the lane's points and paints from it; it repaints the owner
// itself. A hidden owner or Reduce Motion lands at once. Message thread only.
namespace synth::ui {

class LanePointGlide {
public:
    static constexpr double kOutMs = 110.0;
    static constexpr double kInMs = 160.0;
    static constexpr double kMeltMs = 200.0;
    static constexpr double kMeltReducedMs = 80.0;

    explicit LanePointGlide(juce::Component& owner);
    ~LanePointGlide();

    // Takes `now` as the lane's points with no animation (a new lane, a first sync).
    void reset(std::vector<LaneBreakpoint> now);
    // The lane's points are now `now`; animates when this only removed or only added points.
    void pointsChanged(std::vector<LaneBreakpoint> now);

    // Arms the next change of points as a melt of [startBeat, endBeat]; `meltArea` gives the pixel rectangle to
    // repaint.
    void meltNext(double startBeat, double endBeat, std::function<juce::Rectangle<int>()> meltArea);
    // Forgets an armed melt that no change of points followed.
    void disarmMelt() noexcept { meltArmed_ = false; }
    bool isMelting() const noexcept { return running_ && melting_; }
    double meltStartBeat() const noexcept { return meltStart_; }
    double meltEndBeat() const noexcept { return meltEnd_; }

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

    // Tests: animate as if the owner were on screen, so no native window is needed.
    void forceAnimateForTest(bool force) noexcept { forceAnimateForTest_ = force; }

private:
    bool animates() const;
    bool animatesMelt() const;
    void finish();
    void repaintOwner();

    juce::Component& owner_;
    std::vector<LaneBreakpoint> shown_;
    std::vector<LaneBreakpoint> before_;
    std::vector<LaneBreakpoint> leaving_;
    std::vector<LaneBreakpoint> entering_;
    bool running_ = false;
    bool meltArmed_ = false;
    bool melting_ = false;
    double meltStart_ = 0.0;
    double meltEnd_ = 0.0;
    std::function<juce::Rectangle<int>()> meltArea_;
    bool forceAnimateForTest_ = false;
    float amount_ = 1.0f;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver driver_;
};

} // namespace synth::ui
