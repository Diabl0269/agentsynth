// KeyedPointTween.h -- eases the DRAWN position of keyed points to where they now rest: the geometry is final at
// once, the drawn point trails it by an offset that shrinks to zero over kMs (easeOutCubic). Used when a keyboard
// step, a snapped step or an undo moves points by a discrete jump.
//
// The owner must outlive it, adds offsetFor(key) to a point's resting position when it paints, and is repainted by it
// (the union of where the moving points were and are, through onDirty when set). Plain (no offsets at all) under
// Reduce Motion, Animations Off or a hidden owner. Message thread only.
#pragma once

#include "UI/Layout/UIAnimation.h"
#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace synth::ui {

class KeyedPointTween {
public:
    using Key = std::int64_t;
    static constexpr double kMs = 160.0;

    explicit KeyedPointTween(juce::Component& owner);
    ~KeyedPointTween();

    /** The point `key` now rests at `to`; it is drawn at `from` (where it is drawn right now, see drawnAt) and eases
     *  there. A key already moving restarts from `from`, so pass drawnAt(key, oldResting) to continue smoothly. */
    void retarget(Key key, juce::Point<float> from, juce::Point<float> to);
    /** Forgets `key` (it lands where it rests). */
    void erase(Key key);
    void clear();

    /** How far from its resting place `key` is drawn now ((0, 0) when it is not moving). */
    juce::Point<float> offsetFor(Key key) const;
    juce::Point<float> drawnAt(Key key, juce::Point<float> resting) const { return resting + offsetFor(key); }
    bool isAnimating() const noexcept { return !entries_.empty(); }

    /** Asked to repaint instead of the owner's own repaint(rect); may be null. */
    std::function<void(juce::Rectangle<int>)> onDirty;
    /** How far past a moving point's centre the dirty rectangle reaches, in pixels. */
    void setPaddingPx(int px) noexcept { paddingPx_ = px; }

    /** Tests: animate as if the owner were on screen, step the clock like AnimationDriver::stepForTest, or land. */
    void forceAnimateForTest(bool force) noexcept { forceAnimateForTest_ = force; }
    void stepForTest(double ms) { driver_.stepForTest(ms); }
    void completeForTest() { driver_.completeForTest(); }

private:
    struct Entry {
        juce::Point<float> offset; // at progress 0 of the running tween
        juce::Point<float> resting;
    };

    bool animates() const;
    void bakeOffsets();
    void startRun();
    void onFrame(float eased);
    void repaintAround(const std::vector<juce::Point<float>>& points);
    std::vector<juce::Point<float>> drawnPoints() const;

    juce::Component& owner_;
    std::map<Key, Entry> entries_;
    float eased_ = 0.0f;
    int paddingPx_ = 12;
    bool forceAnimateForTest_ = false;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver driver_;
};

} // namespace synth::ui
