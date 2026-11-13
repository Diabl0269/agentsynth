#pragma once

#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <juce_graphics/juce_graphics.h>

// ExitEnterTimeline.h (docs/layout/animation.md "Delete and undo animation"): the one timeline every surface that
// animates a delete or its undo shares. Pure, header-only, no components and no clock, so it is unit-tested headlessly.
//
//   delete   : exit (the item shrinks toward its centre, 180 ms)  ->  gap (the others close up, 200 ms)
//   Cmd+Z    : gap (the others make room, 200 ms)  ->  grow (the item grows back from its centre, 180 ms)
//              ->  outline (a 1 px accent outline around it fades, 400 ms)
//
// The phases run one after another, never together, so no frame shows two items overlapping. A phase a change does
// not have (nothing moved, nothing deleted) has zero length. Reduce Motion swaps the shrink/grow for a plain fade
// (`ghostScale` stays 1, `ghostAlpha` carries the progress); the gap and outline phases are unchanged.
//
// `together` is the one exception to "never together": a copy that comes out of its original (or goes back into it on
// undo) and the others making room are one movement. Every phase then reads the same eased progress over one span
// (docs/layout/duplicate-motion.md).
namespace synth::ui {

struct ExitEnterTimeline {
    static constexpr double kExitMs = 180.0;
    static constexpr double kGapMs = 200.0;
    static constexpr double kGrowMs = 180.0;
    static constexpr double kOutlineMs = 400.0;
    static constexpr double kOutOfSourceMs = 240.0;
    static constexpr double kIntoSourceMs = 200.0;
    static constexpr double kTogetherFadeMs = 80.0; // Reduce Motion: the rows are in place, the copy only fades

    bool hasExit = false;
    bool hasGap = false;
    bool hasEnter = false;
    /** The fading outline after the grow marks something an undo brought back; a card that is simply new has none. */
    bool outline = true;
    /** The grow overshoots by the 8% bounce (easeOutBackGrow) and settles, the way a new card lands. */
    bool bounce = false;
    /** One movement over `togetherMs`: eased progress for exit, gap and grow alike (`reverse`: going back into the
     *  original, eased in, else coming out of it, eased out). No outline. */
    bool together = false;
    bool reverse = false;
    double togetherMs = kOutOfSourceMs;

    /** Eased progress of each phase, 0..1. A phase that is absent reads 1 once the time is past its slot. */
    struct Frame {
        float exit = 0.0f;
        float gap = 0.0f;
        float grow = 0.0f;
        float outline = 0.0f;  // linear: the outline's alpha is 1 - outline
        bool grown = false;    // the grow phase is over (a bouncing grow passes 1 before it ends)
        bool together = false; // the frame of a `together` timeline: gap, exit and grow are one progress
    };

    double exitMs() const noexcept { return hasExit ? kExitMs : 0.0; }
    double gapMs() const noexcept { return hasGap ? kGapMs : 0.0; }
    double growMs() const noexcept { return hasEnter ? kGrowMs : 0.0; }
    double outlineMs() const noexcept { return hasEnter && outline ? kOutlineMs : 0.0; }

    double totalMs() const noexcept { return together ? togetherMs : exitMs() + gapMs() + growMs() + outlineMs(); }

    /** Where each phase stands `elapsedMs` after the animation began. */
    Frame at(double elapsedMs) const noexcept {
        Frame f;
        if (together) {
            const float t = static_cast<float>(std::clamp(togetherMs > 0.0 ? elapsedMs / togetherMs : 1.0, 0.0, 1.0));
            f.exit = f.gap = f.grow = reverse ? easeInCubic(t) : easeOutCubic(t);
            f.grown = t >= 1.0f;
            f.together = true;
            f.outline = 1.0f;
            return f;
        }
        double start = 0.0;
        f.exit = phase(elapsedMs, start, exitMs(), easeInCubic);
        start += exitMs();
        f.gap = phase(elapsedMs, start, gapMs(), easeOutCubic);
        start += gapMs();
        f.grow = bounce ? phase(elapsedMs, start, growMs(), easeOutBackGrow)
                        : phase(elapsedMs, start, growMs(), easeOutCubic);
        start += growMs();
        f.grown = elapsedMs >= start;
        f.outline = phase(elapsedMs, start, outlineMs(), [](float t) { return t; });
        return f;
    }

    /** The scale a shrinking (or growing) item is drawn at: 1 -> 0 on exit, 0 -> 1 on grow; always 1 for a fade. */
    static float ghostScale(float progress, bool exiting, bool reducedMotion) noexcept {
        if (reducedMotion)
            return 1.0f;
        return exiting ? 1.0f - progress : progress;
    }

    /** The opacity a shrinking (or growing) item is drawn at: 1 unless it is a plain fade. */
    static float ghostAlpha(float progress, bool exiting, bool reducedMotion) noexcept {
        if (!reducedMotion)
            return 1.0f;
        return exiting ? 1.0f - progress : progress;
    }

    /** `r` scaled about its centre. */
    static juce::Rectangle<float> scaledAboutCentre(juce::Rectangle<int> r, float scale) noexcept {
        const auto c = r.toFloat().getCentre();
        const float w = static_cast<float>(r.getWidth()) * scale;
        const float h = static_cast<float>(r.getHeight()) * scale;
        return {c.x - w * 0.5f, c.y - h * 0.5f, w, h};
    }

private:
    template <typename Ease>
    static float phase(double elapsedMs, double start, double length, Ease ease) noexcept {
        if (length <= 0.0)
            return elapsedMs >= start ? 1.0f : 0.0f;
        const float t = static_cast<float>(std::clamp((elapsedMs - start) / length, 0.0, 1.0));
        return ease(t);
    }
};

} // namespace synth::ui
