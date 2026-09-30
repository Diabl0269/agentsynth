#include "AppLookAndFeel.h"
#include "UI/Layout/FocusRing.h"

namespace synth::theme {

// Concern: rotary slider drawing (the linear slider / fader lives in AppLookAndFeelFader.cpp).

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

    // 6. Keyboard focus ring around the whole knob.
    synth::ui::paintFocusRing(g, juce::Rectangle<float>(size, size).withCentre(centre), slider, size * 0.5f);
}

} // namespace synth::theme
