#include "AppLookAndFeel.h"
#include "KnobPainterInternal.h"

namespace synth::theme {

// Concern: the knob looks (Settings > Appearance > Controls) -- the shared geometry, the helpers more
// than one style draws with, the dispatcher, and the Classic, Neon and Ring painters. Chunky and Analog
// live in AppLookAndFeelKnobStylesChunky.cpp / AppLookAndFeelKnobStylesAnalog.cpp.
// AppLookAndFeelSliders.cpp keeps the dim layer and the focus ring around this.

namespace knobs {

juce::Point<float> Geo::pointAt(float radius, float angle) const noexcept {
    return {centre.x + std::sin(angle) * radius, centre.y - std::cos(angle) * radius};
}

juce::Rectangle<float> Geo::discBounds(float radius) const noexcept {
    return juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
}

float Geo::angleAt(float pos01) const noexcept { return startAngle + pos01 * (endAngle - startAngle); }

// Every style lays out from one square: the arc radius, body radius and 270 degree sweep are shared, so
// a style change never moves the value's angle. `light` is what the "lights up" styles brighten with:
// the raw value for an ordinary knob, the distance from the centre for a bipolar one, so a bipolar knob
// at zero is dark and full left is as bright as full right.
Geo makeGeo(const Theme& theme, juce::Rectangle<float> bounds, float pos01, float origin01) {
    Geo geo;
    geo.pos = juce::jlimit(0.0f, 1.0f, pos01);
    geo.origin = juce::jlimit(0.0f, 1.0f, origin01);
    geo.size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    geo.half = geo.size * 0.5f;
    geo.centre = bounds.getCentre();
    geo.trackWidth = theme.metrics.knobTrackWidth;
    geo.arcRadius = geo.half - geo.trackWidth;
    geo.bodyRadius = geo.size * 0.26f;
    geo.startAngle = AppLookAndFeel::kRotaryStart;
    geo.endAngle = AppLookAndFeel::kRotaryEnd;
    geo.valueAngle = geo.angleAt(geo.pos);
    geo.originAngle = geo.angleAt(geo.origin);
    const float reach = juce::jmax(geo.origin, 1.0f - geo.origin);
    geo.light = reach > 0.0f ? juce::jlimit(0.0f, 1.0f, std::abs(geo.pos - geo.origin) / reach) : 0.0f;
    return geo;
}

juce::PathStrokeType roundedStroke(float width) {
    return juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
}

juce::Path arcPath(const Geo& geo, float radius, float fromAngle, float toAngle) {
    juce::Path path;
    path.addCentredArc(geo.centre.x, geo.centre.y, radius, radius, 0.0f, fromAngle, toAngle, true);
    return path;
}

// A bipolar knob's arc runs from 12 o'clock towards the value on either side; at the centre value the
// path is empty, so nothing is stroked (a zero-length arc would still paint a round cap).
juce::Path valueArc(const Geo& geo, float radius) {
    const float from = juce::jmin(geo.originAngle, geo.valueAngle);
    const float to = juce::jmax(geo.originAngle, geo.valueAngle);
    if (to - from < 1.0e-4f)
        return {};
    return arcPath(geo, radius, from, to);
}

void fillGlow(juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float alpha) {
    if (alpha <= 0.0f || radius <= 0.0f)
        return;
    juce::ColourGradient glow(colour.withMultipliedAlpha(juce::jmin(1.0f, alpha)), centre, colour.withAlpha(0.0f),
                              centre.translated(radius, 0.0f), true);
    glow.addColour(0.35, colour.withMultipliedAlpha(juce::jmin(1.0f, alpha) * 0.7f));
    g.setGradientFill(glow);
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));
}

void fillDisc(juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour) {
    g.setColour(colour);
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));
}

void fillDiscShadow(juce::Graphics& g, juce::Point<float> centre, float radius, float blur, float alpha) {
    const float outer = radius + blur;
    juce::ColourGradient shadow(juce::Colours::black.withAlpha(alpha), centre, juce::Colours::transparentBlack,
                                centre.translated(outer, 0.0f), true);
    shadow.addColour(juce::jlimit(0.0, 0.99, (double)((radius - blur) / outer)), juce::Colours::black.withAlpha(alpha));
    g.setGradientFill(shadow);
    g.fillEllipse(juce::Rectangle<float>(outer * 2.0f, outer * 2.0f).withCentre(centre));
}

bool isLightTheme(const Theme& theme) noexcept { return theme.colors.bg0.getPerceivedBrightness() > 0.5f; }

namespace {

void drawTrack(juce::Graphics& g, const Geo& geo, const Theme& theme) {
    g.setColour(theme.colors.border);
    g.strokePath(arcPath(geo, geo.arcRadius, geo.startAngle, geo.endAngle), roundedStroke(geo.trackWidth));
}

// The theme's glow halo under a value arc (nothing when the theme has no glow).
void drawArcGlow(juce::Graphics& g, const Theme& theme, const juce::Path& value, juce::Colour colour, float width) {
    if (theme.treatment.glow <= 0.0f || value.isEmpty())
        return;
    g.setColour(colour.withAlpha(theme.treatment.glow * 0.5f));
    g.strokePath(value, roundedStroke(width * 2.5f));
}

} // namespace

// The shared track and the value arc with the theme's glow, used by Classic and Chunky.
void drawTrackAndValueArc(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    drawTrack(g, geo, theme);
    const auto value = valueArc(geo, geo.arcRadius);
    drawArcGlow(g, theme, value, valueColour, geo.trackWidth);
    g.setColour(valueColour);
    g.strokePath(value, roundedStroke(geo.trackWidth));
}

namespace {

// Classic's glowing tip at the value end of the arc (brighter the further the value is from its origin)
// and the small tick marking 12 o'clock.
void drawClassicTipAndTick(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    const auto tip = geo.pointAt(geo.arcRadius, geo.valueAngle);
    fillGlow(g, tip, geo.size * 0.095f, valueColour, 0.15f + 0.7f * geo.light);
    fillDisc(g, tip, juce::jmax(1.0f, geo.size * 0.018f), juce::Colours::white.withAlpha(0.8f * geo.light));

    g.setColour(theme.colors.textMuted.withAlpha(0.6f));
    g.drawLine(juce::Line<float>(geo.pointAt(geo.half - 6.0f, 0.0f), geo.pointAt(geo.half - 9.0f, 0.0f)), 1.2f);
}

// Classic's body: a deep radial gradient lit from the top left, a soft drop shadow and a bevel rim.
void drawClassicBody(juce::Graphics& g, const Geo& geo, const Theme& theme) {
    const auto& c = theme.colors;
    const float r = geo.bodyRadius;
    fillDiscShadow(g, geo.centre.translated(0.0f, geo.size * 0.012f), r, geo.size * 0.02f, 0.45f);

    const auto body = geo.discBounds(r);
    const juce::Point<float> focal(body.getX() + body.getWidth() * 0.36f, body.getY() + body.getHeight() * 0.3f);
    juce::ColourGradient fill(c.surfaceHi.interpolatedWith(juce::Colours::white, 0.1f), focal, c.knobBody.darker(0.4f),
                              focal.translated(body.getWidth() * 0.95f, 0.0f), true);
    fill.addColour(0.5, c.surfaceHi);
    g.setGradientFill(fill);
    g.fillEllipse(body);
    g.setColour(c.border);
    g.drawEllipse(body, theme.metrics.borderWidth);

    const auto bevel = geo.discBounds(r - 1.2f);
    juce::ColourGradient rim(juce::Colours::white.withAlpha(0.45f), bevel.getTopLeft(),
                             juce::Colours::black.withAlpha(0.35f), bevel.getBottomRight(), false);
    rim.addColour(0.5, juce::Colours::transparentWhite);
    g.setGradientFill(rim);
    g.drawEllipse(bevel, 1.3f);
}

} // namespace

void paintClassic(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    drawTrackAndValueArc(g, geo, theme, valueColour);
    drawClassicTipAndTick(g, geo, theme, valueColour);
    drawClassicBody(g, geo, theme);
    g.setColour(theme.colors.knobPointer);
    juce::Path pointer;
    pointer.startNewSubPath(geo.centre);
    pointer.lineTo(geo.pointAt(geo.bodyRadius * 0.92f, geo.valueAngle));
    g.strokePath(pointer, roundedStroke(2.0f));

    const auto& tr = theme.treatment;
    if (tr.style == ThemeStyle::Textured && tr.texture > 0.0f) {
        g.setColour(theme.colors.border.withAlpha(tr.texture * 0.25f));
        for (int i = 1; i <= 3; ++i)
            g.drawEllipse(geo.discBounds(geo.bodyRadius * (0.4f + 0.18f * (float)i)), 0.6f);
    }
}

// Neon has no body: a thick glowing arc, a bright tip and the value as a number in the middle, all
// dim at the origin and brightening with `light`. A theme with more glow than the floor brightens it
// further (neonGlowStrength); a theme with none still glows.
void paintNeon(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    const float radius = geo.half - 6.0f * juce::jmin(1.0f, geo.size / 54.0f);
    const float width = geo.size * 0.07f;
    const float boost = neonGlowStrength(theme, 1.0f) / kNeonMinGlow;
    const float bloom = juce::jmin(1.0f, 0.95f * geo.light * boost);

    g.setColour(valueColour.withAlpha(0.14f));
    g.strokePath(arcPath(geo, radius, geo.startAngle, geo.endAngle), roundedStroke(width));

    const auto value = valueArc(geo, radius);
    const float bloomWidths[] = {2.6f, 1.9f, 1.3f};
    const float bloomAlphas[] = {0.2f, 0.3f, 0.45f};
    for (int i = 0; i < 3; ++i) {
        g.setColour(valueColour.withAlpha(bloom * bloomAlphas[i]));
        g.strokePath(value, roundedStroke(width * bloomWidths[i]));
    }
    g.setColour(valueColour.withAlpha(0.4f + 0.6f * geo.light));
    g.strokePath(value, roundedStroke(width));
    g.setColour(juce::Colours::white.withAlpha(0.7f * geo.light));
    g.strokePath(value, roundedStroke(width * 0.3f));

    const auto tip = geo.pointAt(radius, geo.valueAngle);
    fillGlow(g, tip, geo.size * 0.15f, valueColour, juce::jmin(1.0f, (0.2f + 0.8f * geo.light) * boost));
    fillDisc(g, tip, geo.size * 0.05f, juce::Colours::white.withAlpha(0.55f + 0.45f * geo.light));

    // The number: percent of the way from the origin to the end, signed for a bipolar knob.
    const float reach = juce::jmax(geo.origin, 1.0f - geo.origin);
    const int percent = (int)std::lround((geo.pos - geo.origin) / reach * 100.0f);
    const float textHeight = geo.size * 0.2f;
    fillGlow(g, geo.centre, textHeight * 1.3f, valueColour, 0.25f * geo.light * boost);
    // A light page washes a faint number out, so it starts brighter there.
    const float floor = isLightTheme(theme) ? 0.7f : 0.45f;
    g.setColour(valueColour.withAlpha(floor + (1.0f - floor) * geo.light));
    g.setFont(juce::Font(juce::FontOptions(theme.type.monoFamily, textHeight, juce::Font::plain)));
    g.drawText(juce::String(percent), geo.discBounds(geo.half * 0.7f), juce::Justification::centred, false);
}

namespace {

// How lit LED `fraction` (0..1 along the sweep) is: fully once the value has passed it, partly for the
// one it is passing, and, for a bipolar knob, only on the value's side of the centre.
float ledLevel(const Geo& geo, float fraction, int ledCount) {
    const float steps = (float)(ledCount - 1);
    if (!geo.bipolar())
        return juce::jlimit(0.0f, 1.0f, (geo.pos - fraction) * steps + 1.0f);
    if (geo.pos > geo.origin && fraction > geo.origin + 1.0e-4f)
        return juce::jlimit(0.0f, 1.0f, (geo.pos - fraction) * steps + 1.0f);
    if (geo.pos < geo.origin && fraction < geo.origin - 1.0e-4f)
        return juce::jlimit(0.0f, 1.0f, (fraction - geo.pos) * steps + 1.0f);
    return 0.0f;
}

} // namespace

// Ring: fifteen LEDs round a small dark body. They light one by one as the value rises (the leading
// one part lit) and their glow grows with `light`; a bipolar knob keeps its top LED as a pale zero mark.
void paintRing(juce::Graphics& g, const Geo& geo, const Theme& theme, juce::Colour valueColour) {
    constexpr int kLedCount = 15;
    const auto& c = theme.colors;
    const float ledRadius = juce::jmax(1.2f, geo.size * 0.035f);
    for (int i = 0; i < kLedCount; ++i) {
        const float fraction = (float)i / (float)(kLedCount - 1);
        const auto at = geo.pointAt(geo.arcRadius, geo.angleAt(fraction));
        const bool zeroMark = geo.bipolar() && std::abs(fraction - geo.origin) < 1.0e-3f;
        fillDisc(g, at, ledRadius, zeroMark ? c.textPrimary.withAlpha(0.85f) : c.border);
        const float level = ledLevel(geo, fraction, kLedCount);
        if (level <= 0.0f)
            continue;
        fillGlow(g, at, geo.size * 0.12f, valueColour, level * geo.light * 0.75f);
        fillDisc(g, at, ledRadius, valueColour.withAlpha(level * (0.35f + 0.65f * geo.light)));
    }

    const auto body = geo.discBounds(geo.size * 0.21f);
    g.setColour(c.bg0);
    g.fillEllipse(body);
    g.setColour(c.border);
    g.drawEllipse(body, 1.0f);
    fillDisc(g, geo.pointAt(geo.size * 0.15f, geo.valueAngle), juce::jmax(1.2f, geo.size * 0.03f), c.textPrimary);
}

} // namespace knobs

void paintKnob(juce::Graphics& g, const Theme& theme, KnobStyle style, juce::Rectangle<float> knobBounds, float pos01,
               juce::Colour valueColour, float origin01) {
    const auto geo = knobs::makeGeo(theme, knobBounds, pos01, origin01);
    switch (style) {
    case KnobStyle::Classic:
        knobs::paintClassic(g, geo, theme, valueColour);
        break;
    case KnobStyle::Hardware:
        knobs::paintChunky(g, geo, theme, valueColour);
        break;
    case KnobStyle::Analog:
        knobs::paintAnalog(g, geo, theme);
        break;
    case KnobStyle::Neon:
        knobs::paintNeon(g, geo, theme, valueColour);
        break;
    case KnobStyle::Ring:
        knobs::paintRing(g, geo, theme, valueColour);
        break;
    }
}

} // namespace synth::theme
