// KeyedTween.h -- eases the DRAWN place (a point or a rectangle) of keyed things to where they now rest: the geometry
// is final at once, the drawn value trails it by an offset that shrinks to zero over kMs (easeOutCubic). Used when a
// keyboard step, a snapped step or an undo moves things by a discrete jump.
//
// The owner must outlive it, adds offsetFor(key) to a thing's resting place when it paints (drawnAt), and is
// repainted by it (the union of where the moving things were and are, through onDirty when set). Plain (no offsets at
// all) under Reduce Motion, Animations Off or a hidden owner. Message thread only.
#pragma once

#include "UI/Layout/UIAnimation.h"
#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <vector>

namespace synth::ui {

/** Arithmetic a tweened value type needs: offsets are differences that scale toward zero. */
template <typename T>
struct TweenMath;
template <>
struct TweenMath<juce::Point<float>> {
    static juce::Point<float> sub(juce::Point<float> a, juce::Point<float> b) { return a - b; }
    static juce::Point<float> add(juce::Point<float> a, juce::Point<float> b) { return a + b; }
    static juce::Point<float> scale(juce::Point<float> a, float f) { return a * f; }
    static juce::Rectangle<float> bounds(juce::Point<float> p) { return {p, p}; }
};
template <>
struct TweenMath<juce::Rectangle<float>> {
    using R = juce::Rectangle<float>;
    static R sub(R a, R b) {
        return {a.getX() - b.getX(), a.getY() - b.getY(), a.getWidth() - b.getWidth(), a.getHeight() - b.getHeight()};
    }
    static R add(R a, R b) {
        return {a.getX() + b.getX(), a.getY() + b.getY(), a.getWidth() + b.getWidth(), a.getHeight() + b.getHeight()};
    }
    static R scale(R a, float f) { return {a.getX() * f, a.getY() * f, a.getWidth() * f, a.getHeight() * f}; }
    static R bounds(R r) { return r; }
};

template <typename T>
class KeyedTween {
public:
    using Value = T;
    using Key = std::int64_t;
    static constexpr double kMs = 160.0;

    explicit KeyedTween(juce::Component& owner);
    ~KeyedTween();

    /** The thing `key` now rests at `to`; it is drawn at `from` (where it is drawn right now, see drawnAt) and eases
     *  there. A key already moving restarts from `from`, so pass drawnAt(key, oldResting) to continue smoothly.
     *  With `fadeWhenReduced` (a restore), Reduce Motion lands the thing at `to` at once and fades it in there over
     *  control_motion::kReducedMs (alphaFor); Animations Off and a hidden owner land it plain. */
    void retarget(Key key, T from, T to, bool fadeWhenReduced = false);
    /** Forgets `key` (it lands where it rests). */
    void erase(Key key);
    void clear();

    /** How far from its resting place `key` is drawn now (all zero when it is not moving). */
    T offsetFor(Key key) const;
    T drawnAt(Key key, T resting) const { return TweenMath<T>::add(resting, offsetFor(key)); }
    /** How opaque `key` is drawn now: below 1 only while its Reduce Motion fade runs. */
    float alphaFor(Key key) const;
    bool isFading() const noexcept { return !fading_.empty(); }
    bool isAnimating() const noexcept { return !entries_.empty() || !fading_.empty(); }

    /** Asked to repaint instead of the owner's own repaint(rect); may be null. */
    std::function<void(juce::Rectangle<int>)> onDirty;
    /** How far past a moving point's centre the dirty rectangle reaches, in pixels. */
    void setPaddingPx(int px) noexcept { paddingPx_ = px; }

    /** Tests: animate as if the owner were on screen, step the clock like AnimationDriver::stepForTest, or land. */
    void forceAnimateForTest(bool force) noexcept { forceAnimateForTest_ = force; }
    void stepForTest(double ms) {
        driver_.stepForTest(ms);
        fadeDriver_.stepForTest(ms);
    }
    void completeForTest() {
        driver_.completeForTest();
        fadeDriver_.completeForTest();
    }

private:
    struct Entry {
        T offset; // at progress 0 of the running tween
        T resting;
    };

    bool animates() const;
    bool fadesInstead() const;
    void startFade(Key key, T to);
    void bakeOffsets();
    void startRun();
    void onFrame(float eased);
    void repaintAround(const std::vector<T>& values);
    std::vector<T> drawnValues() const;

    juce::Component& owner_;
    std::map<Key, Entry> entries_;
    float eased_ = 0.0f;
    int paddingPx_ = 12;
    bool forceAnimateForTest_ = false;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver driver_;
    std::map<Key, T> fading_; // keys fading in at their resting place (Reduce Motion)
    float fadeEased_ = 0.0f;
    juce::VBlankAnimatorUpdater fadeVblank_;
    AnimationDriver fadeDriver_;
};

using KeyedPointTween = KeyedTween<juce::Point<float>>;
using KeyedRectTween = KeyedTween<juce::Rectangle<float>>;

extern template class KeyedTween<juce::Point<float>>;
extern template class KeyedTween<juce::Rectangle<float>>;

} // namespace synth::ui
