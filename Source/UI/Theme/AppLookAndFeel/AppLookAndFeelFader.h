#pragma once

// The one fader drawing every linear juce::Slider uses (mixer strip, controller-surface cell,
// module-card horizontal sliders). Split out of AppLookAndFeel so the geometry and the state
// handling can be exercised without a live Slider (docs/layout/theming.md, "Linear sliders").

#include "UI/Theme/KnobStyle.h"
#include "UI/Theme/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::theme::fader {

// Geometry of one fader size. capW / capH are in screen axes (a vertical fader's cap is wider than
// tall, a horizontal fader's cap taller than wide).
struct Metrics {
    float slot;   // slot thickness across the travel
    float capW;   // cap width  (x extent)
    float capH;   // cap height (y extent)
    float radius; // cap corner radius
    float groove; // centre-line length across the cap
};

// Interaction state resolved from the Slider by the look-and-feel; tests set it directly.
struct State {
    bool hover = false;    // pointer over, not pressed
    bool dragging = false; // mouse button down on the slider
    bool focused = false;  // keyboard focus
    bool enabled = true;
    bool dimmed = false; // greyed out but operable (a layout's dim)
    // The look: the user's knob style (faders always match the knobs) and the fill colour (a card's
    // module-family hue, else accent). A transparent colour means accent.
    KnobStyle style = KnobStyle::Classic;
    juce::Colour valueColour{};
};

// Size choice from the slider's real bounds. A vertical slider at least kLargeMinWidth wide and
// kLargeMinHeight tall gets the large mixer fader; any other vertical slider the small one (the
// 56 px controller-surface cell); a horizontal slider the medium one. The cap shrinks to fit
// bounds that are smaller than its nominal size.
constexpr int kLargeMinWidth = 64;
constexpr int kLargeMinHeight = 40;
Metrics metricsFor(bool vertical, int width, int height);

// Distance from the slider's end to the cap centre at value 0 / 1: half the cap's length along the
// travel plus a 2 px margin, so the cap is never clipped. Feeds getSliderThumbRadius().
int thumbRadiusFor(bool vertical, int width, int height);

// Paints the fader. `travel` is the rectangle juce::Slider hands drawLinearSlider (already inset by
// thumbRadiusFor()); `sliderSize` is the whole slider's width / height, which picks the size;
// `sliderPos` is the cap centre along the travel, in the same coordinates.
void paint(juce::Graphics&, const Theme&, juce::Rectangle<int> travel, juce::Point<int> sliderSize, float sliderPos,
           bool vertical, const State&);

} // namespace synth::theme::fader
