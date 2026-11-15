#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/MotionOrigin.h"
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

    /** A block that was just made grows out of `origin`, anchored at `anchorBeat`; several can grow at once. One still
     *  leaving starts from the size it is drawn at. Reduce Motion: a plain fade instead. */
    void startGrow(synth::PlacementId id, MotionOrigin origin, double anchorBeat);
    /** Size fraction (0..1) the block is drawn at, and its opacity; 1 and 1 when it is not growing. */
    float growScale(synth::PlacementId id) const;
    float growAlpha(synth::PlacementId id) const;
    /** The origin the block's latest grow started from (Centre when it never grew). */
    MotionOrigin growOrigin(synth::PlacementId id) const;
    /** `rect` (the block at rest) as drawn now; `xAtBeat` maps the grow's anchor beat to the strip. */
    template <typename XAtBeat>
    juce::Rectangle<float> growRect(synth::PlacementId id, juce::Rectangle<float> rect, XAtBeat&& xAtBeat) const {
        const auto it = grows_.find(id.value);
        const float scale = growScale(id);
        if (it == grows_.end() || scale >= 1.0f)
            return rect;
        return scaledTowardOrigin(rect, scale, it->second.origin, (float)xAtBeat(it->second.anchorBeat));
    }

    /** A block that has just gone shrinks toward `origin`, anchored at `anchorX` (a picture, the doc is already
     *  changed); several can leave at once. One still growing starts from the size it is drawn at. Reduce Motion: it
     *  fades instead. */
    struct Ghost {
        juce::Rectangle<float> bounds;
        juce::Colour colour;
        juce::String name;
        float alpha = 1.0f; // the opacity it was last drawn with; its exit starts from there
    };
    void startExit(synth::PlacementId id, const Ghost& ghost, MotionOrigin origin, float anchorX);
    /** Every leaving picture on screen now, with the scale, opacity and rectangle it is drawn at. */
    struct ExitFrame {
        Ghost ghost;
        float scale = 1.0f;
        float alpha = 1.0f;
        juce::Rectangle<float> rect;
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
        float from = 0.0f; // the size it started at: 0, or where a reversed exit was drawn
        MotionOrigin origin = MotionOrigin::Centre;
        double anchorBeat = 0.0;
        bool fade = false;
    };
    std::map<std::int64_t, Grow> grows_; // by placement id; a landed one is dropped at the next start
    double growMs_ = 0.0;

    struct Exit {
        Ghost ghost;
        AnimationDriver driver;
        float t = 0.0f;
        float from = 1.0f; // the size it started at: 1, or where a reversed grow was drawn
        MotionOrigin origin = MotionOrigin::Centre;
        float anchorX = 0.0f;
        bool fade = false;
        bool active = false;
        float scale() const { return fade ? 1.0f : from * (1.0f - exitProgress(origin, t)); }
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
