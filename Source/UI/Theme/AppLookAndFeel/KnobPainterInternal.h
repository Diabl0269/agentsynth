#pragma once

// Shared between the knob-style painter units (AppLookAndFeelKnobStyles*.cpp): the geometry every style
// is laid out from and the small drawing helpers more than one style uses. Defined in
// AppLookAndFeelKnobStyles.cpp.

#include "KnobPainter.h"

namespace synth::theme::knobs {

struct Geo {
    juce::Point<float> centre;
    float size = 0.0f;       // the knob square's side, px
    float half = 0.0f;       // size / 2
    float arcRadius = 0.0f;  // the shared track radius: half - knobTrackWidth
    float bodyRadius = 0.0f; // 0.26 * size
    float trackWidth = 0.0f;
    float startAngle = 0.0f;
    float endAngle = 0.0f;
    float valueAngle = 0.0f;
    float originAngle = 0.0f; // where the value arc starts: the sweep start, or 12 o'clock when bipolar
    float pos = 0.0f;         // value 0..1
    float origin = 0.0f;      // 0, or 0.5 for a bipolar knob
    float light = 0.0f;       // 0..1: how far the value is from its origin, relative to the furthest it can go

    juce::Point<float> pointAt(float radius, float angle) const noexcept;
    juce::Rectangle<float> discBounds(float radius) const noexcept;
    float angleAt(float pos01) const noexcept;
    bool bipolar() const noexcept { return origin > 0.0f; }
};

Geo makeGeo(const Theme& theme, juce::Rectangle<float> bounds, float pos01, float origin01);

juce::PathStrokeType roundedStroke(float width);
// The arc at `radius` from the origin to the value (empty when they coincide).
juce::Path valueArc(const Geo& geo, float radius);
juce::Path arcPath(const Geo& geo, float radius, float fromAngle, float toAngle);
// A soft round glow (a radial gradient standing in for a blur), `alpha` at the centre, clear at `radius`.
void fillGlow(juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float alpha);
// A blurred drop shadow under a disc: solid black at `alpha` to `radius - blur`, clear at `radius + blur`.
void fillDiscShadow(juce::Graphics& g, juce::Point<float> centre, float radius, float blur, float alpha);
// The shared border track and the value arc (from the origin) with the theme's glow: Classic and Chunky.
void drawTrackAndValueArc(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour);
void fillDisc(juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour);
// True for a light theme (the page background is bright), where printed marks need dark ink.
bool isLightTheme(const Theme& theme) noexcept;

void paintClassic(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour);
void paintChunky(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour);
void paintAnalog(juce::Graphics& g, const Geo& geo, const Theme& theme);
void paintNeon(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour);
void paintRing(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour);

} // namespace synth::theme::knobs
