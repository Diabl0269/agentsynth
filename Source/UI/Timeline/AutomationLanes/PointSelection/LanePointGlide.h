#pragma once

#include "LanePointSelection.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// LanePointGlide -- keeps the points an automation lane editor last showed, so a change that only removes points (a
// delete, a redo) or only brings points back (an undo, a paste, an add) can be drawn as a short animation instead of a
// jump: the points that go shrink and fade out over 110 ms, the points that come grow in over 160 ms, and the line
// glides from the shape it was drawn in to the new one on the same clock (lineWeight); under Reduce Motion the points
// only fade and the old line cross-fades into the new one (80 ms). Any other change (a move, a stroke) lands at once,
// unless the owner armed a melt (meltNext): then the next change of points animates whatever it did, as the old curve
// melting into the new one inside a beat range over motionMs(200, 80), repainting only that range. Changes fed before
// the melt's first frame (an edit that sets several points one by one) join it.
//
// The owner must outlive it, feeds it every change of the lane's points and paints from it; it repaints the owner
// itself. A hidden owner or Animations Off lands at once. Message thread only.
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
    // The lane's points are now `now`; animates when this only removed or only added points. True when that started
    // a glide of points coming or going (the owner then morphs its line from what it drew).
    bool pointsChanged(std::vector<LaneBreakpoint> now);

    // Arms the next change of points as a melt of [startBeat, endBeat]; `meltArea` gives the pixel rectangle to
    // repaint.
    void meltNext(double startBeat, double endBeat, std::function<juce::Rectangle<int>()> meltArea);
    // Forgets an armed melt that no change of points followed.
    void disarmMelt() noexcept { meltArmed_ = false; }
    bool isMelting() const noexcept { return running_ && melting_; }
    double meltStartBeat() const noexcept { return meltStart_; }
    double meltEndBeat() const noexcept { return meltEnd_; }

    // The points as last fed in (what the lane showed before the change being fed now).
    const std::vector<LaneBreakpoint>& shown() const noexcept { return shown_; }

    bool isRunning() const noexcept { return running_; }
    // Eased progress: 0 where the change started, 1 settled.
    float amount() const noexcept;
    // How much of the old line is drawn in a glide of points coming or going: 1 where it started, 0 settled.
    float lineWeight() const noexcept;
    // True while the running change only fades (Reduce Motion): no dot changes size and the line does not morph.
    bool fadesOnly() const noexcept { return running_ && fadeOnly_; }
    // The points before the running change (curve cross-fade source).
    const std::vector<LaneBreakpoint>& before() const noexcept { return before_; }
    // How present the point at `beat` is right now: 1 for one that stays, shrinking to 0 for one leaving, growing from
    // 0 for one arriving.
    float presence(double beat) const;
    // The points leaving in the running change, still to be drawn.
    const std::vector<LaneBreakpoint>& leaving() const noexcept { return leaving_; }

    // Tests: animate as if the owner were on screen, so no native window is needed; step the clock like
    // AnimationDriver::stepForTest.
    void forceAnimateForTest(bool force) noexcept { forceAnimateForTest_ = force; }
    void stepForTest(double ms) { driver_.stepForTest(ms); }

private:
    bool animates() const;
    void finish();
    void repaintOwner();
    void startFades(std::vector<LaneBreakpoint> gone, std::vector<LaneBreakpoint> arrived,
                    const std::vector<LaneBreakpoint>& now);

    juce::Component& owner_;
    std::vector<LaneBreakpoint> shown_;
    std::vector<LaneBreakpoint> before_;
    std::vector<LaneBreakpoint> leaving_;
    std::vector<LaneBreakpoint> entering_;
    std::vector<float> leavingFrom_;  // the presence each leaving point starts its fade out at
    std::vector<float> enteringFrom_; // the presence each entering point starts its fade in at
    bool running_ = false;
    bool leaves_ = false;
    bool fadeOnly_ = false;
    bool meltArmed_ = false;
    bool melting_ = false;
    double meltStart_ = 0.0;
    double meltEnd_ = 0.0;
    std::function<juce::Rectangle<int>()> meltArea_;
    bool forceAnimateForTest_ = false;
    float progress_ = 1.0f; // linear 0..1 of the running change
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver driver_;
};

} // namespace synth::ui
