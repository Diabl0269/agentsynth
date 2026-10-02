#pragma once

// MacroHullGlide.h
//
// Paint-time glide of expanded macro borders. A macro's border is a live union of its members' cards, so a
// membership change (a module dragged in or out) or a drop that releases a border held still during a drag
// would make it jump; this holds each changed border part of the way back toward where it was and lets it
// settle over one short tween. Pure state like MacroCrossingAnimator: GraphEditor owns the driver and calls
// applyTweenAt()/finish(), which is also what lets a test drive it with no VBlank.

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

class MacroHullGlide {
public:
    using Hulls = std::map<juce::String, juce::Rectangle<int>>;

    /** Arms a glide for every macro present in both maps whose border moved; true when anything was armed. */
    bool arm(const Hulls& before, const Hulls& after);
    /** `t` is the driver's eased 0..1 progress. */
    void applyTweenAt(float t) noexcept;
    /** Drops every glide; borders show their live bounds again. */
    void finish() noexcept;
    bool isLive() const noexcept { return !from_.empty(); }
    /** The border to draw for `macroId` whose live bounds are `target`. */
    juce::Rectangle<int> apply(const juce::String& macroId, juce::Rectangle<int> target) const;

private:
    Hulls from_, to_;
    float progress_ = 1.0f;
};
