#pragma once

#include "MixerRowsMotion.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include <functional>
#include <map>
#include <vector>

// MixerRowLookGlide.h (docs/mixer/row-motion.md "An undo glides what the forward edit animated"): the rows of an insert
// or send list that stay through an undo or redo but look different (they moved, dimmed or brightened, or got another
// name) ease from the look they were drawn with to the one they rest at. The lists are new objects after every edit, so
// the old list's drawn looks travel in the column's snapshot and the new list starts this glide from them.
//
// One KeyedRectTween per list, keyed by row index: x is the dim (0 bright, 1 dimmed), y the row's top, width how much
// of the OLD name still shows (1 to 0, a cross-fade). Reduce Motion lands the rows at once and fades them in (the
// tween's own fade); Animations Off and a list that is not on screen land plain.
namespace synth::ui {

class MixerRowLookGlide {
public:
    /** `onFrame` runs on every frame (the owner re-places its controls and repaints). */
    MixerRowLookGlide(juce::Component& owner, std::function<void()> onFrame);

    /** Eases every row of `resting` that `before` also has (by key) and that looks different. False when no row does.
     *  `dimOnly` (a forward edit) eases only the dim of a row that kept its name: the edit placed everything else.
     */
    bool start(const std::vector<MixerRowLook>& before, const std::vector<MixerRowLook>& resting, bool dimOnly = false);
    void finishNow();
    bool isRunning() const noexcept { return tween_.isAnimating(); }

    /** How far above or below its resting place row `row` is drawn now. */
    float yOffsetFor(int row) const;
    /** The dim row `row` is drawn with now, given the one it rests at. */
    float dimFor(int row, float restingDim) const;
    /** 1, or less while the row's Reduce Motion fade runs. */
    float alphaFor(int row) const;
    /** How much of the old name still shows (0 when the name did not change or the glide has landed). */
    float oldNameWeightFor(int row) const;
    juce::String oldNameFor(int row) const;

    /** Where each of `resting`'s rows is drawn right now (a new glide that starts mid-way continues from these). */
    std::vector<MixerRowLook> drawn(const std::vector<MixerRowLook>& resting) const;

    /** Tests: animate as if on screen, step the clock, or land. */
    void forceAnimateForTest(bool force) noexcept { tween_.forceAnimateForTest(force); }
    void stepForTest(double ms) { tween_.stepForTest(ms); }
    void completeForTest() { tween_.completeForTest(); }

private:
    KeyedRectTween tween_;
    std::map<int, juce::String> oldNames_;
};

} // namespace synth::ui
