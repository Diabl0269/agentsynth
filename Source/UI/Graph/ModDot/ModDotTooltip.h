#pragma once

// The small "LFO 1 . +42%" readout a mod-dot drag (or a key step) shows above-right of its knob. Painted
// by the canvas on top of the cards (GraphContentComponent::paintOverChildren), never a child
// component, so a card's clip never cuts it. It fades in over 160 ms and out over 110 ms (a plain 80 ms
// fade under Reduce Motion). docs/layout/animation.md ("What moves, and how").

#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/** Where the tooltip of size `size` goes: its bottom-left corner 2 px right of and above the knob's
 *  top-right corner, nudged to stay inside `viewport`. Never overlaps `knob` -- when the nudge would
 *  push it onto the knob (a knob at the viewport's top edge) it drops below-right of the knob instead.
 *  A pure function of rectangles, so the placement is unit-tested. All rectangles share one space. */
juce::Rectangle<float> modDotTooltipRect(juce::Rectangle<float> knob, juce::Point<float> size,
                                         juce::Rectangle<float> viewport);

class ModDotTooltip {
public:
    static constexpr double kFadeInMs = 160.0;
    static constexpr double kFadeOutMs = 110.0;
    static constexpr double kReducedFadeMs = 80.0;

    /** `canvas` is the component the tooltip is painted on; it must outlive this. */
    explicit ModDotTooltip(juce::Component& canvas);
    ~ModDotTooltip();

    /** Shows "<name> . <signed percent>" for `amount` beside `knob` (a component whose bounds are
     *  mapped into the canvas). Fades in when hidden, otherwise re-texts at the current opacity. */
    void show(juce::Component& knob, const juce::String& name, float amount);
    /** Fades out; still drawn where it was until it has gone. */
    void hide();

    void paint(juce::Graphics& g);

    bool isShown() const noexcept { return wanted_; }
    float getOpacity() const noexcept { return opacity_; }
    juce::String getText() const { return content_ ? content_->text : juce::String(); }
    /** Where it is drawn now, canvas coordinates; empty when there is nothing to draw. */
    juce::Rectangle<float> getBounds() const;

private:
    struct Content {
        juce::Component::SafePointer<juce::Component> knob;
        juce::String text;
        bool positive = true;
    };

    bool animates() const;
    void fadeTo(float target, double fullMs, std::function<float(float)> easing);
    void setOpacity(float value);
    juce::Point<float> sizeFor(const juce::String& text) const;

    juce::Component& canvas_;
    std::optional<Content> content_; // kept through a fade-out so it keeps drawing
    bool wanted_ = false;
    float opacity_ = 0.0f;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver fade_;
};

} // namespace synth::ui
