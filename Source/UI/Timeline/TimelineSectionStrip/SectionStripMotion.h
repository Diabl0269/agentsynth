#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
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
    static constexpr double kHintSwapMs = 120.0;

    /** `host` is the strip: it must outlive this and supplies the VBlank clock. */
    SectionStripMotion(juce::Component& host, std::function<void()> onFrame);
    ~SectionStripMotion();

    /** A block that was just made fills in from its left (or right) edge; several can grow at once. Reduce Motion: a
     *  plain fade instead. */
    void startGrow(synth::PlacementId id, bool fromRight);
    /** Width fraction (0..1) the block is drawn at, and its opacity; 1 and 1 when it is not growing. */
    float growScale(synth::PlacementId id) const;
    float growAlpha(synth::PlacementId id) const;
    bool growsFromRight(synth::PlacementId id) const;

    /** A block that has just gone shrinks toward its centre where it was (a picture, the doc is already changed);
     *  several can leave at once. Reduce Motion: it fades instead. */
    struct Ghost {
        juce::Rectangle<float> bounds;
        juce::Colour colour;
        juce::String name;
    };
    void startExit(synth::PlacementId id, const Ghost& ghost);
    /** Every leaving picture on screen now, with the scale and opacity it is drawn at. */
    struct ExitFrame {
        Ghost ghost;
        float scale = 1.0f;
        float alpha = 1.0f;
    };
    std::vector<ExitFrame> exitFrames() const;
    bool hasExitGhost() const noexcept;

    /** The lifted look of a dragged block: 0 at rest, 1 fully raised. Eases up over kLiftMs and back over kSettleMs;
     *  under Reduce Motion (movement has no fade form) it lands at once. */
    void liftTo(bool lifted);
    float lift() const noexcept { return lift_; }

    /** Stamped placements flash a 1 px accent outline that fades over kOutlineMs. */
    void startOutline(const std::vector<synth::PlacementId>& placements);
    bool isOutlined(synth::PlacementId id) const;
    float outlineAlpha() const noexcept { return outlineAlpha_; }

    /** The drop hint changed its words (Shift flipped the drop mode): the old text fades out as the new one fades in.
     *  hintSwap() runs 0 to 1; it is 1 at rest and lands at once when animation is off. */
    void startHintSwap();
    float hintSwap() const noexcept { return hintSwap_; }

    bool isAnimating() const noexcept;
    /** Lands everything at once (a new doc, a destroyed strip). */
    void stopAll();

    // Test seams: the duration each effect last started with, and stepping every running effect by hand.
    double getGrowDurationMsForTest() const noexcept { return growMs_; }
    double getOutlineDurationMsForTest() const noexcept { return outlineMs_; }
    bool isGrowingForTest() const noexcept;
    bool isLiftingForTest() const noexcept { return liftDriver_.isRunning(); }
    bool isOutliningForTest() const noexcept { return outline_.isRunning(); }
    bool isHintSwappingForTest() const noexcept { return hintSwapDriver_.isRunning(); }
    bool isExitingForTest() const noexcept;
    /** How many blocks have started an exit since the strip was made: each removal counts once. */
    int getExitsStartedForTest() const noexcept { return exitsStarted_; }
    void stepForTest(double ms);

private:
    bool canAnimate() const;
    void frame() const {
        if (onFrame_)
            onFrame_();
    }
    void pruneLanded();

    juce::Component& host_;
    std::function<void()> onFrame_;
    juce::VBlankAnimatorUpdater updater_;

    struct Grow {
        AnimationDriver driver;
        float t = 1.0f;
        bool fromRight = false;
        bool fade = false;
    };
    std::map<std::int64_t, Grow> grows_; // by placement id; a landed one is dropped at the next start
    double growMs_ = 0.0;

    struct Exit {
        Ghost ghost;
        AnimationDriver driver;
        float t = 0.0f;
        bool fade = false;
        bool active = false;
    };
    std::map<std::int64_t, Exit> exits_; // by placement id; a landed one is dropped at the next start
    int exitsStarted_ = 0;

    AnimationDriver liftDriver_;
    float lift_ = 0.0f;

    AnimationDriver hintSwapDriver_;
    float hintSwap_ = 1.0f;

    AnimationDriver outline_;
    std::vector<synth::PlacementId> outlined_;
    float outlineAlpha_ = 0.0f;
    double outlineMs_ = 0.0;
};

} // namespace synth::ui
