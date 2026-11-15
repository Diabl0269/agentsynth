// RestoredColourGlide.h -- the recipe every surface that shows a model colour follows so the colour eases on an undo
// or redo and nowhere else (docs/layout/undo-glide.md, "Colours glide too"): a KeyedColourTween plus the RestoreWatch
// of the app's undo manager. The owner calls takeRestore() once per sync from its model, then note() for each colour it
// re-read, and paints through drawn(). A live preview or an ordinary edit is never a restore, so it stays instant.
// Message thread only.
#pragma once

#include "AppUndoManager.h"
#include "UI/Layout/KeyGlide/KeyedColourTween.h"
#include "UI/Layout/KeyGlide/RestoreWatch.h"

namespace synth::ui {

class RestoredColourGlide {
public:
    using Key = KeyedColourTween::Key;

    explicit RestoredColourGlide(juce::Component& owner)
        : tween_(owner) {}

    /** Starts a sync level with `undo`'s restore serial; null leaves every sync an ordinary one. */
    void setUndoManager(AppUndoManager* undo) {
        undo_ = undo;
        watch_ = RestoreWatch(undo != nullptr ? undo->getRestoreSerial() : 0);
    }
    /** True when this sync belongs to an undo or redo. Once per sync, however many colours it re-reads. */
    bool takeRestore() { return undo_ != nullptr && watch_.take(undo_->isRestoring(), undo_->getRestoreSerial()); }

    /** `key` was drawn from `was` and now rests at `now`: a restore eases it, an ordinary change lands it at once. A
     *  colour that did not change keeps whatever glide it is in. */
    void note(Key key, juce::Colour was, juce::Colour now, bool restored) {
        if (was == now)
            return;
        tween_.forceAnimateForTest(animateOffScreenForTest());
        if (restored)
            tween_.retarget(key, tween_.drawnAt(key, TweenColour::from(was)), TweenColour::from(now));
        else
            tween_.erase(key);
    }
    /** A colour that appears on a thing built during a restore (a replaced widget) eases from where its predecessor
     *  was drawn. */
    void glideFrom(Key key, juce::Colour from, juce::Colour now) {
        tween_.forceAnimateForTest(animateOffScreenForTest());
        if (from != now)
            tween_.retarget(key, TweenColour::from(from), TweenColour::from(now));
    }
    void forget(Key key) { tween_.erase(key); }

    /** The colour to paint for `key`, which rests at `resting`: derive every fill, stripe and shade from it. */
    juce::Colour drawn(Key key, juce::Colour resting) const {
        return tween_.drawnAt(key, TweenColour::from(resting)).toColour();
    }
    bool isGliding() const noexcept { return tween_.isAnimating(); }
    void stepForTest(double ms) { tween_.stepForTest(ms); }
    /** Tests: every glide runs as if its owner were on screen (a fixture builds surfaces nobody shows). */
    static void setAnimateOffScreenForTest(bool on) noexcept { animateOffScreenForTest() = on; }
    /** Called on every frame, retarget and erase with the area the owner would repaint (for a child that paints a
     *  colour of its own). Takes over the owner's repaint, so it must repaint the owner itself. */
    void setOnDirty(std::function<void(juce::Rectangle<int>)> onDirty) { tween_.onDirty = std::move(onDirty); }

private:
    KeyedColourTween tween_;
    RestoreWatch watch_;
    AppUndoManager* undo_ = nullptr;
    static bool& animateOffScreenForTest() noexcept {
        static bool on = false;
        return on;
    }
};

} // namespace synth::ui
