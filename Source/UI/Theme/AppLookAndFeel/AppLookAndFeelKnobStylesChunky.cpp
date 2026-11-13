#include "KnobPainterInternal.h"

namespace synth::theme::knobs {

// Concern: the Chunky knob (persisted id "hardware"): a toy-synth knob with a ten-lobed flower skirt in
// the value colour that turns with the value, a cream cap and a fat dark pointer, inside the shared
// track and value arc. Every size is a fraction of the knob square, so it reads the same at card size.

namespace {

constexpr int kLobes = 10;

// The flower outline at rest (a lobe at 12 o'clock), rotated to the value. Lobes are rounded and the
// valleys between them pinched, from a sharpened |cos| profile.
juce::Path flowerPath(const Geo& geo) {
    const float outer = geo.size * 0.37f;
    const float valley = geo.size * 0.28f;
    constexpr int kPoints = 160;
    juce::Path flower;
    for (int i = 0; i < kPoints; ++i) {
        const float angle = juce::MathConstants<float>::twoPi * (float)i / (float)kPoints;
        const float lobe = std::pow(std::abs(std::cos(angle * (float)kLobes * 0.5f)), 0.6f);
        const auto p = geo.pointAt(valley + (outer - valley) * lobe, angle + geo.valueAngle);
        if (i == 0)
            flower.startNewSubPath(p);
        else
            flower.lineTo(p);
    }
    flower.closeSubPath();
    return flower;
}

void drawFlower(juce::Graphics& g, const Geo& geo, juce::Colour valueColour) {
    const auto flower = flowerPath(geo);
    g.setColour(valueColour);
    g.fillPath(flower);

    // Shading lit from the top left: a highlight fading out by the middle, a darker far rim.
    const auto box = geo.discBounds(geo.size * 0.37f);
    const juce::Point<float> focal(box.getX() + box.getWidth() * 0.35f, box.getY() + box.getHeight() * 0.3f);
    juce::ColourGradient shade(juce::Colours::white.withAlpha(0.35f), focal, juce::Colours::black.withAlpha(0.3f),
                               focal.translated(box.getWidth() * 0.85f, 0.0f), true);
    shade.addColour(0.55, juce::Colours::transparentWhite);
    g.setGradientFill(shade);
    g.fillPath(flower);

    g.setColour(valueColour.interpolatedWith(juce::Colours::black, 0.4f));
    g.strokePath(flower, juce::PathStrokeType(juce::jmax(1.0f, geo.size * 0.012f), juce::PathStrokeType::curved));
}

void drawCap(juce::Graphics& g, const Geo& geo, juce::Colour valueColour) {
    const auto cap = geo.discBounds(geo.size * 0.22f);
    const juce::Point<float> focal(cap.getX() + cap.getWidth() * 0.38f, cap.getY() + cap.getHeight() * 0.32f);
    juce::ColourGradient cream(juce::Colour(0xfffffdf7), focal, juce::Colour(0xffcdbfa4),
                               focal.translated(cap.getWidth() * 0.85f, 0.0f), true);
    cream.addColour(0.7, juce::Colour(0xffefe6d4));
    g.setGradientFill(cream);
    g.fillEllipse(cap);
    g.setColour(valueColour.interpolatedWith(juce::Colour(0xff3a2a1a), 0.5f));
    g.drawEllipse(cap, juce::jmax(1.0f, geo.size * 0.015f));

    // A glossy highlight, tilted, on the cap's upper left.
    juce::Path gloss;
    const float rx = geo.size * 0.09f;
    const float ry = geo.size * 0.045f;
    gloss.addEllipse(-rx, -ry, rx * 2.0f, ry * 2.0f);
    gloss.applyTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::pi / 6.0f)
                             .translated(geo.centre.x - geo.size * 0.05f, geo.centre.y - geo.size * 0.08f));
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.fillPath(gloss);
}

} // namespace

void paintChunky(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    drawTrackAndValueArc(g, geo, theme, valueColour);
    fillDiscShadow(g, geo.centre.translated(0.0f, geo.size * 0.04f), geo.size * 0.33f, geo.size * 0.03f, 0.55f);
    drawFlower(g, geo, valueColour);
    drawCap(g, geo, valueColour);

    juce::Path pointer;
    pointer.startNewSubPath(geo.pointAt(geo.size * 0.03f, geo.valueAngle));
    pointer.lineTo(geo.pointAt(geo.size * 0.18f, geo.valueAngle));
    g.setColour(juce::Colour(0xff2a2320));
    g.strokePath(pointer, roundedStroke(geo.size * 0.055f));
}

} // namespace synth::theme::knobs
