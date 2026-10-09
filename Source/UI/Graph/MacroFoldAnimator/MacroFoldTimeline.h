#pragma once

#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <juce_graphics/juce_graphics.h>
#include <vector>

// MacroFoldTimeline.h (docs/layout/animation.md "Macro fold"): the pure timing and geometry of a macro folding its
// modules into the closed card (collapse) or unfolding them out of it (expand). No components and no clock, so it is
// unit-tested headlessly, like ExitEnterTimeline.h.
//
// Module i starts `i * stagger` after the animation begins and flies for `kFlightMs`; the stagger shrinks for a big
// macro so the last module still lands by `kTotalMs`. Expanding is the exact reverse: the module that left last comes
// out first. The border is drawn as `outlineRect`: the card-to-border blend UNIONED with every module's current rect,
// so no frame ever shows a module outside it.
namespace synth::ui::macro_fold {

constexpr double kTotalMs = 320.0;   // the last module has landed by here
constexpr double kStaggerMs = 35.0;  // gap between one module leaving and the next
constexpr double kFlightMs = 200.0;  // one module's flight
constexpr double kCableMs = 160.0;   // a cable drawing out of its port once its module has landed (expand)
constexpr double kFadeMs = 80.0;     // Reduce Motion: the plain fade
constexpr double kHandoverMs = 80.0; // collapse: the real card takes over from the ghosts in this last stretch

struct Timeline {
    int count = 0;
    bool collapsing = true;
    bool withCables = false; // expand only: cables draw out after the last landing

    double staggerMs() const noexcept {
        return count > 1 ? std::min(kStaggerMs, (kTotalMs - kFlightMs) / static_cast<double>(count - 1)) : 0.0;
    }
    /** Where module `index` (its place in the macro's member order) sits in the order the modules leave or arrive. */
    int order(int index) const noexcept { return collapsing ? index : count - 1 - index; }
    double startMs(int index) const noexcept { return static_cast<double>(order(index)) * staggerMs(); }
    double landMs(int index) const noexcept { return startMs(index) + kFlightMs; }
    /** When the last module lands (never past kTotalMs). */
    double modulesEndMs() const noexcept {
        return count > 0 ? static_cast<double>(count - 1) * staggerMs() + kFlightMs : 0.0;
    }
    /** When everything has settled: an expand's last cable finishes kCableMs after the last landing. */
    double totalMs() const noexcept { return modulesEndMs() + (!collapsing && withCables ? kCableMs : 0.0); }

    /** Module `index`'s linear flight progress at `ms`, 0..1. */
    float flight(int index, double ms) const noexcept {
        return static_cast<float>(std::clamp((ms - startMs(index)) / kFlightMs, 0.0, 1.0));
    }
    /** Eased position along the way from the start rect to the end rect: it accelerates into the box when folding and
     *  lands with the 8% bounce when unfolding. May pass 1 briefly. */
    float eased(int index, double ms) const noexcept {
        const float p = flight(index, ms);
        return collapsing ? easeInCubic(p) : easeOutBackGrow(p);
    }
    /** A cable that touches a module drawing out of its port over kCableMs from the moment it has landed; 0..1. */
    static float cableDraw(double landedMs, double ms) noexcept {
        return easeOutCubic(static_cast<float>(std::clamp((ms - landedMs) / kCableMs, 0.0, 1.0)));
    }

    /** The border's blend between the open border and the card: it shrinks in parallel with the modules when folding
     *  and runs ahead of them (done by ~60% of the flights) when unfolding. 0 = open border, 1 = card. */
    float outlineToCard(double ms) const noexcept {
        const double span = std::max(1.0, modulesEndMs());
        if (collapsing)
            return easeInOutCubic(static_cast<float>(std::clamp(ms / span, 0.0, 1.0)));
        return 1.0f - easeOutCubic(static_cast<float>(std::clamp(ms / (span * 0.6), 0.0, 1.0)));
    }
};

inline juce::Rectangle<float> lerpRect(juce::Rectangle<float> a, juce::Rectangle<float> b, float t) noexcept {
    auto lerp = [t](float x, float y) { return x + (y - x) * t; };
    return {lerp(a.getX(), b.getX()), lerp(a.getY(), b.getY()), lerp(a.getWidth(), b.getWidth()),
            lerp(a.getHeight(), b.getHeight())};
}

/** The border drawn around a folding macro: the blend of `open` and `card`, grown to hold every rect in `members`.
 *  A hair larger than the union, so float rounding in `width = right - x` can never leave a module's edge outside. */
inline juce::Rectangle<float> outlineRect(juce::Rectangle<float> open, juce::Rectangle<float> card, float toCard,
                                          const std::vector<juce::Rectangle<float>>& members) noexcept {
    auto outline = lerpRect(open, card, toCard);
    for (const auto& r : members)
        outline = outline.getUnion(r);
    return outline.expanded(0.01f);
}

} // namespace synth::ui::macro_fold
