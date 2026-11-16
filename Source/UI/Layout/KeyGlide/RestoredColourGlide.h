// RestoredColourGlide.h -- the recipe every surface that shows a model colour follows so the colour eases on an undo
// or redo and nowhere else (docs/layout/undo-glide.md, "Colours glide too"): a KeyedColourTween plus the RestoreWatch
// of the app's undo manager. The owner calls takeRestore() once per sync from its model, then note() for each colour it
// re-read, and paints through drawn(). A live preview or an ordinary edit is never a restore, so it stays instant.
// Message thread only.
#pragma once

#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class AppUndoManager;

namespace synth::ui {

class RestoredColourGlide {
public:
    using Key = std::int64_t;

    explicit RestoredColourGlide(juce::Component& owner);
    ~RestoredColourGlide();
    RestoredColourGlide(RestoredColourGlide&&) noexcept;
    RestoredColourGlide& operator=(RestoredColourGlide&&) noexcept;

    /** Starts a sync level with `undo`'s restore serial; null leaves every sync an ordinary one. */
    void setUndoManager(AppUndoManager* undo);
    /** True when this sync belongs to an undo or redo. Once per sync, however many colours it re-reads. */
    bool takeRestore();

    /** `key` was drawn from `was` and now rests at `now`: a restore eases it, an ordinary change lands it at once. A
     *  colour that did not change keeps whatever glide it is in. */
    void note(Key key, juce::Colour was, juce::Colour now, bool restored);
    /** A colour that appears on a thing built during a restore (a replaced widget) eases from where its predecessor
     *  was drawn. */
    void glideFrom(Key key, juce::Colour from, juce::Colour now);
    void forget(Key key);

    /** The colour to paint for `key`, which rests at `resting`: derive every fill, stripe and shade from it. */
    juce::Colour drawn(Key key, juce::Colour resting) const;
    bool isGliding() const noexcept;
    void stepForTest(double ms);
    /** Tests: every glide runs as if its owner were on screen (a fixture builds surfaces nobody shows). */
    static void setAnimateOffScreenForTest(bool on) noexcept;
    /** Called on every frame, retarget and erase with the area the owner would repaint (for a child that paints a
     *  colour of its own). Takes over the owner's repaint, so it must repaint the owner itself. */
    void setOnDirty(std::function<void(juce::Rectangle<int>)> onDirty);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace synth::ui
