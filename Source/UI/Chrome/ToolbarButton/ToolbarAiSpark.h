#pragma once

#include <juce_graphics/juce_graphics.h>

// The AI button's looping glyph parts (docs/layout/icons.md#the-ai-button): a pulse of signal that
// runs along the patch cable to the plug, a glow at the plug and the spark that blooms there. The
// cable and plug are the icon's SVG; these three are drawn in code because they move along a path and
// fade, which a hover transform on an SVG group cannot do. One cycle is kToolbarAiCycleMs long and the
// keyframes below are fractions of it.
namespace synth::ui {

inline constexpr double kToolbarAiCycleMs = 2800.0;

// Everything that changes during the cycle, in the icon's 24-unit grid.
struct ToolbarAiFrame {
    float pulseAlong = 0.0f; // 0 at the cable's start .. 1 at the plug
    float pulseAlpha = 0.0f;
    float glowAlpha = 0.0f;
    float sparkScale = 1.0f; // 1 is the spark at its full radius
    float sparkDegrees = 0.0f;
    float sparkAlpha = 1.0f;
};

// The keyframes at `phase` (0..1 through one cycle).
ToolbarAiFrame toolbarAiFrame(float phase);
// The glyph at rest: no pulse or glow, the spark small but visible.
ToolbarAiFrame toolbarAiRestFrame();
// `rest` blended into `playing` by `amount` (0..1).
ToolbarAiFrame toolbarAiBlend(const ToolbarAiFrame& rest, const ToolbarAiFrame& playing, float amount);

// The cable's centre line and the point a fraction `along` of its length from its start.
const juce::Path& toolbarAiCablePath();
juce::Point<float> toolbarAiPulsePoint(float along);

struct ToolbarAiColours {
    juce::Colour pulse;
    juce::Colour glow;
    juce::Colour spark;
};

// Draws glow, pulse and spark for `frame` through `iconToScreen`, all at `alpha` on top.
void paintToolbarAiParts(juce::Graphics& g, const juce::AffineTransform& iconToScreen, const ToolbarAiFrame& frame,
                         const ToolbarAiColours& colours, float alpha);

} // namespace synth::ui
