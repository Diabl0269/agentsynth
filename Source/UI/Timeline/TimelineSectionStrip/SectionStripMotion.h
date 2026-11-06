#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// SectionStripMotion: the Sections strip's time-bounded motion, one AnimationDriver per effect on the strip's own
// VBlank updater (no timer; nothing runs once an effect has landed). The strip asks it what to draw and repaints from
// onFrame. docs/timeline/sections.md ("Motion") lists the numbers.
namespace synth::ui {

class SectionStripMotion {
public:
    static constexpr double kGrowMs = 160.0;
    static constexpr double kExitMs = 180.0;
    static constexpr double kLiftMs = 100.0;
    static constexpr double kSettleMs = 140.0;
    static constexpr double kOutlineMs = 400.0;
    static constexpr double kReducedMs = 80.0;

    /** `host` is the strip: it must outlive this and supplies the VBlank clock. */
    SectionStripMotion(juce::Component& host, std::function<void()> onFrame);
    ~SectionStripMotion();

    /** A block that was just made fills in from its left (or right) edge. Reduce Motion: a plain fade instead. */
    void startGrow(synth::PlacementId id, bool fromRight);
    /** Width fraction (0..1) the block is drawn at, and its opacity; 1 and 1 when it is not growing. */
    float growScale(synth::PlacementId id) const;
    float growAlpha(synth::PlacementId id) const;
    bool growsFromRight() const noexcept { return growFromRight_; }

    /** A block that has just been deleted shrinks toward its centre where it was (a picture, the doc is already
     *  changed). Reduce Motion: it fades instead. */
    struct Ghost {
        juce::Rectangle<float> bounds;
        juce::Colour colour;
        juce::String name;
    };
    void startExit(const Ghost& ghost);
    bool hasExitGhost() const noexcept { return exitActive_; }
    const Ghost& exitGhost() const noexcept { return exit_; }
    float exitScale() const noexcept;
    float exitAlpha() const noexcept;

    /** The lifted look of a dragged block: 0 at rest, 1 fully raised. Eases up over kLiftMs and back over kSettleMs;
     *  under Reduce Motion (movement has no fade form) it lands at once. */
    void liftTo(bool lifted);
    float lift() const noexcept { return lift_; }

    /** Stamped placements flash a 1 px accent outline that fades over kOutlineMs. */
    void startOutline(const std::vector<synth::PlacementId>& placements);
    bool isOutlined(synth::PlacementId id) const;
    float outlineAlpha() const noexcept { return outlineAlpha_; }

    bool isAnimating() const noexcept;
    /** Lands everything at once (a new doc, a destroyed strip). */
    void stopAll();

    // Test seams: the duration each effect last started with, and stepping every running effect by hand.
    double getGrowDurationMsForTest() const noexcept { return growMs_; }
    double getOutlineDurationMsForTest() const noexcept { return outlineMs_; }
    bool isGrowingForTest() const noexcept { return grow_.isRunning(); }
    bool isLiftingForTest() const noexcept { return liftDriver_.isRunning(); }
    bool isOutliningForTest() const noexcept { return outline_.isRunning(); }
    bool isExitingForTest() const noexcept { return exitDriver_.isRunning(); }
    void stepForTest(double ms);

private:
    bool canAnimate() const;
    void frame() const {
        if (onFrame_)
            onFrame_();
    }

    juce::Component& host_;
    std::function<void()> onFrame_;
    juce::VBlankAnimatorUpdater updater_;

    AnimationDriver grow_;
    synth::PlacementId growId_;
    float growT_ = 1.0f;
    bool growFromRight_ = false;
    bool growFade_ = false;
    double growMs_ = 0.0;

    AnimationDriver exitDriver_;
    Ghost exit_;
    bool exitActive_ = false;
    float exitT_ = 0.0f;
    bool exitFade_ = false;

    AnimationDriver liftDriver_;
    float lift_ = 0.0f;

    AnimationDriver outline_;
    std::vector<synth::PlacementId> outlined_;
    float outlineAlpha_ = 0.0f;
    double outlineMs_ = 0.0;
};

} // namespace synth::ui
