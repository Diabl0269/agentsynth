#pragma once

// ControlMotion.h -- how a control arrives on a card and leaves it. A control the user adds grows in from its
// centre (a fader along its length) with an 8% bounce, 200 ms; a control that is removed shrinks away the same
// way backwards, 150 ms. Reduce Motion: a plain 80 ms fade. The pure frame math and the leaving ghost (a
// picture of the control that shrinks where it was); the owner drives them. docs/layout/animation.md.

#include "UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui::control_motion {

constexpr double kGrowMs = 200.0;
constexpr double kShrinkMs = 150.0;
constexpr double kReducedMs = 80.0;

/** The axis a control scales along: a knob on both, a long fader only along its length. */
enum class Axis { both, horizontal, vertical };

inline Axis axisFor(juce::Rectangle<int> bounds) noexcept {
    if (bounds.getWidth() >= bounds.getHeight() * 2)
        return Axis::horizontal;
    if (bounds.getHeight() >= bounds.getWidth() * 2)
        return Axis::vertical;
    return Axis::both;
}

/** Scale of a control that is arriving at progress `t` (0..1): 0 -> 1, passing 1.08 on the way. */
inline float growScale(float t) noexcept { return easeOutBackGrow(t); }

/** Scale of a control that is leaving at progress `t` (0..1): 1 -> 0. */
inline float shrinkScale(float t) noexcept { return 1.0f - easeInCubic(t); }

/** `scale` about the centre of `bounds`, on `axis` only. */
inline juce::AffineTransform scaleAbout(juce::Rectangle<int> bounds, float scale, Axis axis) noexcept {
    const auto c = bounds.toFloat().getCentre();
    const float sx = axis == Axis::vertical ? 1.0f : scale;
    const float sy = axis == Axis::horizontal ? 1.0f : scale;
    return juce::AffineTransform::translation(-c.x, -c.y).scaled(sx, sy).translated(c.x, c.y);
}

/** A removed control, drawn from the picture taken before it left: it shrinks about its centre (or, under
 *  Reduce Motion, fades in place). Never takes the mouse. The owner sets the progress each frame. */
class ShrinkGhost final : public juce::Component {
public:
    ShrinkGhost(juce::Image image, juce::Rectangle<int> bounds, Axis axis, bool reducedMotion)
        : image_(std::move(image))
        , axis_(axis)
        , reduced_(reducedMotion)
        , startMs_(juce::Time::getMillisecondCounterHiRes()) {
        setInterceptsMouseClicks(false, false);
        setBounds(bounds);
    }

    double durationMs() const noexcept { return reduced_ ? kReducedMs : kShrinkMs; }
    double startMs() const noexcept { return startMs_; }
    float progress() const noexcept { return progress_; }

    void setProgress(float t) {
        progress_ = juce::jlimit(0.0f, 1.0f, t);
        repaint();
    }

    /** The rectangle the picture is drawn in now, in the ghost's own coordinates. */
    juce::Rectangle<float> pictureRect() const noexcept {
        const float scale = reduced_ ? 1.0f : shrinkScale(progress_);
        const float w = (float)getWidth() * (axis_ == Axis::vertical ? 1.0f : scale);
        const float h = (float)getHeight() * (axis_ == Axis::horizontal ? 1.0f : scale);
        return {((float)getWidth() - w) * 0.5f, ((float)getHeight() - h) * 0.5f, w, h};
    }

    float opacity() const noexcept { return reduced_ ? 1.0f - easeInCubic(progress_) : 1.0f; }

    void paint(juce::Graphics& g) override {
        if (image_.isNull() || pictureRect().isEmpty())
            return;
        g.setOpacity(opacity());
        g.drawImage(image_, pictureRect(), juce::RectanglePlacement::stretchToFit);
    }

private:
    juce::Image image_;
    Axis axis_;
    bool reduced_;
    double startMs_;
    float progress_ = 0.0f;
};

} // namespace synth::ui::control_motion
