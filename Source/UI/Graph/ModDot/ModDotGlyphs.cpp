#include "ModDotGlyphButton.h"

namespace synth::ui {

void paintModDotChevron(juce::Graphics& g, juce::Rectangle<float> area, float progress, juce::Colour colour) {
    // Same triangle as the module library's fold chevron: it points down at rest, so it is turned a quarter back
    // while folded.
    juce::Path p;
    p.addTriangle(area.getX(), area.getY(), area.getRight(), area.getY(), area.getCentreX(), area.getBottom());
    p.applyTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi * (1.0f - progress),
                                                     area.getCentreX(), area.getCentreY()));
    g.setColour(colour);
    g.fillPath(p);
}

void paintModDotGlyph(juce::Graphics& g, ModDotGlyph glyph, juce::Rectangle<float> a, juce::Colour colour) {
    g.setColour(colour);
    const juce::PathStrokeType stroke(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    switch (glyph) {
    case ModDotGlyph::Back:
        p.startNewSubPath(a.getRight(), a.getCentreY());
        p.lineTo(a.getX(), a.getCentreY());
        p.startNewSubPath(a.getX() + a.getWidth() * 0.45f, a.getY() + a.getHeight() * 0.15f);
        p.lineTo(a.getX(), a.getCentreY());
        p.lineTo(a.getX() + a.getWidth() * 0.45f, a.getBottom() - a.getHeight() * 0.15f);
        break;
    case ModDotGlyph::Timeline: { // three lanes of different length
        const float h = a.getHeight() / 5.0f;
        const float widths[] = {1.0f, 0.65f, 0.85f};
        for (int i = 0; i < 3; ++i) {
            const float y = a.getY() + h * (0.5f + 2.0f * (float)i);
            p.startNewSubPath(a.getX(), y);
            p.lineTo(a.getX() + a.getWidth() * widths[i], y);
        }
        break;
    }
    case ModDotGlyph::Trash: {
        const float w = a.getWidth();
        p.startNewSubPath(a.getX(), a.getY() + 3.0f);
        p.lineTo(a.getRight(), a.getY() + 3.0f);
        p.startNewSubPath(a.getX() + w * 0.35f, a.getY() + 3.0f);
        p.lineTo(a.getX() + w * 0.35f, a.getY());
        p.lineTo(a.getX() + w * 0.65f, a.getY());
        p.lineTo(a.getX() + w * 0.65f, a.getY() + 3.0f);
        p.startNewSubPath(a.getX() + w * 0.15f, a.getY() + 3.0f);
        p.lineTo(a.getX() + w * 0.22f, a.getBottom());
        p.lineTo(a.getX() + w * 0.78f, a.getBottom());
        p.lineTo(a.getX() + w * 0.85f, a.getY() + 3.0f);
        break;
    }
    }
    g.strokePath(p, stroke);
}

} // namespace synth::ui
