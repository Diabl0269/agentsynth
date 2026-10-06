#include "AppLookAndFeel.h"
#include "KnobPainter.h"
#include "UI/Layout/FocusRing.h"

namespace synth::theme {

// Concern: rotary slider drawing (the linear slider / fader lives in AppLookAndFeelFader.cpp). The
// knob itself is painted by paintKnob() in AppLookAndFeelKnobStyles.cpp.

juce::Colour AppLookAndFeel::knobValueColour(const juce::Component& knob) const {
    if (!knobAppearance.colourByFamily)
        return theme.colors.accent;
    for (const auto* c = &knob; c != nullptr; c = c->getParentComponent()) {
        const auto& family = c->getProperties()[kKnobFamilyProperty];
        if (!family.isVoid())
            return familyHue(theme.colors, (int)family);
    }
    return theme.colors.accent;
}

// A range counts as symmetric when its ends are within 1% of the span of mirroring each other, so a
// float range like -1..1 and an int range like -4..4 both qualify; 0..100 and -12..24 do not.
float knobOriginFor(const juce::Slider& slider) {
    const double min = slider.getMinimum();
    const double max = slider.getMaximum();
    if (!(min < 0.0 && max > 0.0) || std::abs(min + max) > 0.01 * (max - min))
        return 0.0f;
    return juce::jlimit(0.0f, 1.0f, (float)slider.getNormalisableRange().convertTo0to1(0.0));
}

void AppLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPosProportional, float /*rotaryStartAngle*/, float /*rotaryEndAngle*/,
                                      juce::Slider& slider) {
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();

    // A disabled or dimmed (greyed out) knob paints whole at reduced alpha, as a fader does; the focus
    // ring stays outside the layer at full strength.
    const bool disabled = paintsDimmed(slider);
    if (disabled)
        g.beginTransparencyLayer(kDisabledControlAlpha);

    paintKnob(g, theme, knobAppearance.style, bounds, sliderPosProportional, knobValueColour(slider),
              knobOriginFor(slider));

    if (disabled)
        g.endTransparencyLayer();

    // Keyboard focus ring around the whole knob.
    synth::ui::paintFocusRing(g, juce::Rectangle<float>(size, size).withCentre(centre), slider, size * 0.5f);
}

} // namespace synth::theme
