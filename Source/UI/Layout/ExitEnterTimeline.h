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
namespace synth::ui {

struct ExitEnterTimeline {
    static constexpr double kExitMs = 180.0;
    static constexpr double kGapMs = 200.0;
    static constexpr double kGrowMs = 180.0;
    static constexpr double kOutlineMs = 400.0;

    bool hasExit = false;
    bool hasGap = false;
    bool hasEnter = false;

    /** Eased progress of each phase, 0..1. A phase that is absent reads 1 once the time is past its slot. */
    struct Frame {
        float exit = 0.0f;
        float gap = 0.0f;
        float grow = 0.0f;
        float outline = 0.0f; // linear: the outline's alpha is 1 - outline
    };

    double exitMs() const noexcept { return hasExit ? kExitMs : 0.0; }
    double gapMs() const noexcept { return hasGap ? kGapMs : 0.0; }
    double growMs() const noexcept { return hasEnter ? kGrowMs : 0.0; }
    double outlineMs() const noexcept { return hasEnter ? kOutlineMs : 0.0; }

    double totalMs() const noexcept { return exitMs() + gapMs() + growMs() + outlineMs(); }

    /** Where each phase stands `elapsedMs` after the animation began. */
    Frame at(double elapsedMs) const noexcept {
        Frame f;
        double start = 0.0;
        f.exit = phase(elapsedMs, start, exitMs(), easeInCubic);
        start += exitMs();
        f.gap = phase(elapsedMs, start, gapMs(), easeOutCubic);
        start += gapMs();
        f.grow = phase(elapsedMs, start, growMs(), easeOutCubic);
        start += growMs();
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
