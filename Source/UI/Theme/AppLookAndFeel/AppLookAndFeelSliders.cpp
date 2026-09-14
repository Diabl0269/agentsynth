#include "AppLookAndFeel.h"

namespace synth::theme {

// Concern: rotary and linear slider drawing.

void AppLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPosProportional, float /*rotaryStartAngle*/, float /*rotaryEndAngle*/,
                                      juce::Slider& slider) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;
    const auto& tr = theme.treatment;

    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float bodyRadius = size * 0.52f * 0.5f; // ~0.52 * size diameter
    const float arcRadius = size * 0.5f - m.knobTrackWidth;

    const float startAngle = kRotaryStart;
    const float endAngle = kRotaryEnd;
    const float valueAngle = startAngle + sliderPosProportional * (endAngle - startAngle);

    // 1. Track arc.
    {
        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour(c.border);
        g.strokePath(
            track, juce::PathStrokeType(m.knobTrackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 2. Value arc (+ Neon bloom).
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, valueAngle, true);

        if (tr.glow > 0.0f) {
            g.setColour(c.accent.withAlpha(tr.glow * 0.5f));
            g.strokePath(value, juce::PathStrokeType(m.knobTrackWidth * 2.5f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        g.setColour(c.accent);
        g.strokePath(
            value, juce::PathStrokeType(m.knobTrackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 3. Body: radial gradient surfaceHi (38%,32%) -> knobBody.
    {
        const auto bodyBounds = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre);
        juce::Point<float> focal(bodyBounds.getX() + bodyBounds.getWidth() * 0.38f,
                                 bodyBounds.getY() + bodyBounds.getHeight() * 0.32f);
        juce::ColourGradient grad(c.surfaceHi, focal, c.knobBody, bodyBounds.getBottomRight(), true);
        g.setGradientFill(grad);
        g.fillEllipse(bodyBounds);

        g.setColour(c.border);
        g.drawEllipse(bodyBounds, m.borderWidth);
    }

    // 4. Pointer.
    {
        const float pointerLen = bodyRadius * 0.92f; // ~0.46 * diameter
        juce::Point<float> tip(centre.x + std::sin(valueAngle) * pointerLen,
                               centre.y - std::cos(valueAngle) * pointerLen);
        g.setColour(c.knobPointer);
        g.drawLine(juce::Line<float>(centre, tip), 2.0f);
    }

    // 5. Textured striations.
    if (tr.style == ThemeStyle::Textured && tr.texture > 0.0f) {
        g.setColour(c.border.withAlpha(tr.texture * 0.25f));
        for (int i = 1; i <= 3; ++i) {
            const float r = bodyRadius * (0.4f + 0.18f * (float)i);
            g.drawEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(centre), 0.6f);
        }
    }

    juce::ignoreUnused(slider);
}

void AppLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                      float /*minSliderPos*/, float /*maxSliderPos*/, juce::Slider::SliderStyle style,
                                      juce::Slider& slider) {
    const auto& c = theme.colors;
    const bool vertical = (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical);

    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();

    if (vertical) {
        const float trackW = 4.0f;
        auto track = juce::Rectangle<float>(trackW, bounds.getHeight()).withCentre(bounds.getCentre());
        g.setColour(c.surface);
        g.fillRoundedRectangle(track, trackW * 0.5f);

        auto filled = track.withTop(sliderPos);
        g.setColour(c.accent);
        g.fillRoundedRectangle(filled, trackW * 0.5f);

        g.setColour(c.knobPointer);
        g.fillEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre({bounds.getCentreX(), sliderPos}));
    } else {
        const float trackH = 4.0f;
        auto track = juce::Rectangle<float>(bounds.getWidth(), trackH).withCentre(bounds.getCentre());
        g.setColour(c.surface);
        g.fillRoundedRectangle(track, trackH * 0.5f);

        auto filled = track.withRight(sliderPos);
        g.setColour(c.accent);
        g.fillRoundedRectangle(filled, trackH * 0.5f);

        g.setColour(c.knobPointer);
        g.fillEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre({sliderPos, bounds.getCentreY()}));
    }

    juce::ignoreUnused(slider);
}

} // namespace synth::theme
