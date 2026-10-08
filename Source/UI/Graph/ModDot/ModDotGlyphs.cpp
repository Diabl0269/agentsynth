#include "ModDotGlyphButton.h"

namespace synth::ui {

juce::Colour modDotGlyphColour(const ModDotPalette& p, ModDotGlyph glyph, float hover) {
    juce::Colour base = p.accent;
    switch (glyph) {
    case ModDotGlyph::Trash:
        base = p.negative;
        break;
    case ModDotGlyph::Timeline:
    case ModDotGlyph::Crosshair:
        base = p.positive;
        break;
    case ModDotGlyph::List:
    case ModDotGlyph::Hand:
        base = p.accent;
        break;
    }
    return base.brighter(0.25f * juce::jlimit(0.0f, 1.0f, hover));
}

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
    case ModDotGlyph::List: { // three bulleted rows
        const float h = a.getHeight() / 5.0f;
        for (int i = 0; i < 3; ++i) {
            const float y = a.getY() + h * (0.5f + 2.0f * (float)i);
            p.addEllipse(a.getX(), y - 0.8f, 1.6f, 1.6f);
            p.startNewSubPath(a.getX() + a.getWidth() * 0.3f, y);
            p.lineTo(a.getRight(), y);
        }
        break;
    }
    case ModDotGlyph::Crosshair: { // a ring with four ticks: aim at a card
        const auto c = a.getCentre();
        const float r = a.getWidth() * 0.28f;
        p.addEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        const float tick = a.getWidth() * 0.2f;
        p.startNewSubPath(c.x, a.getY());
        p.lineTo(c.x, a.getY() + tick);
        p.startNewSubPath(c.x, a.getBottom());
        p.lineTo(c.x, a.getBottom() - tick);
        p.startNewSubPath(a.getX(), c.y);
        p.lineTo(a.getX() + tick, c.y);
        p.startNewSubPath(a.getRight(), c.y);
        p.lineTo(a.getRight() - tick, c.y);
        break;
    }
    case ModDotGlyph::Hand: { // a pointing hand: touch a control to add it
        const auto at = [&a](float fx, float fy) {
            return juce::Point<float>(a.getX() + a.getWidth() * fx, a.getY() + a.getHeight() * fy);
        };
        p.startNewSubPath(at(0.30f, 0.62f));
        p.lineTo(at(0.30f, 0.14f)); // the index finger, up and over its tip
        p.quadraticTo(at(0.30f, 0.02f), at(0.42f, 0.02f));
        p.quadraticTo(at(0.54f, 0.02f), at(0.54f, 0.14f));
        p.lineTo(at(0.54f, 0.42f)); // the knuckles step down to the right
        p.quadraticTo(at(0.54f, 0.34f), at(0.64f, 0.38f));
        p.quadraticTo(at(0.72f, 0.40f), at(0.74f, 0.50f));
        p.quadraticTo(at(0.80f, 0.46f), at(0.88f, 0.54f));
        p.lineTo(at(0.88f, 0.74f)); // the edge of the palm
        p.quadraticTo(at(0.88f, 0.98f), at(0.62f, 0.98f));
        p.lineTo(at(0.50f, 0.98f)); // the heel of the hand, and the thumb back to the finger
        p.quadraticTo(at(0.34f, 0.98f), at(0.24f, 0.82f));
        p.lineTo(at(0.08f, 0.58f));
        p.quadraticTo(at(0.14f, 0.50f), at(0.22f, 0.56f));
        p.lineTo(at(0.30f, 0.66f));
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
