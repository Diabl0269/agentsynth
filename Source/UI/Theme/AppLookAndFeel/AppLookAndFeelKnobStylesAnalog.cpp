#include "AppLookAndFeel.h"
#include "KnobPainterInternal.h"

namespace synth::theme::knobs {

// Concern: the Analog knob: a realistic machined knob -- knurled black skirt, brushed-metal cap and a
// white line -- over a printed scale. It has no value arc and no value colour (a real knob is not lit):
// the line is the value. The scale's 0 / 5 / 10 numbers (-5 / 0 / +5 for a bipolar knob) are printed
// only above card size; at card size and below just the ticks remain.

namespace {

constexpr int kTickCount = 11;

// The printed ink: cream on a dark theme, the text colour on a light one where cream would vanish.
juce::Colour scaleInk(const Theme& theme) {
    return isLightTheme(theme) ? theme.colors.textPrimary.withAlpha(0.8f) : juce::Colour(0xffe9e3d2);
}

// The knob's radius once the printed numbers (if any) have their margin inside the bounds.
float dialRadius(const Geo& geo, bool numbers) { return numbers ? geo.half - geo.size * 0.08f * 1.5f : geo.half; }

void drawTicks(juce::Graphics& g, const Geo& geo, const Theme& theme, float outer) {
    const float k = juce::jlimit(0.8f, 1.0f, geo.size / 54.0f);
    g.setColour(scaleInk(theme));
    for (int i = 0; i < kTickCount; ++i) {
        const bool major = i == 0 || i == kTickCount / 2 || i == kTickCount - 1;
        const float angle = geo.angleAt((float)i / (float)(kTickCount - 1));
        const float length = (major ? 5.0f : 3.0f) * k;
        g.drawLine(juce::Line<float>(geo.pointAt(outer - 0.5f, angle), geo.pointAt(outer - 0.5f - length, angle)),
                   major ? 1.4f : 0.9f);
    }
}

void drawNumbers(juce::Graphics& g, const Geo& geo, const Theme& theme, float dial) {
    const float textHeight = geo.size * 0.08f;
    const float radius = dial + textHeight * 0.85f;
    const char* labels[3] = {"0", "5", "10"};
    const char* bipolarLabels[3] = {"-5", "0", "+5"};
    const float at[3] = {0.0f, 0.5f, 1.0f};
    g.setColour(scaleInk(theme));
    g.setFont(AppLookAndFeel::uiSemiBoldFont(textHeight));
    for (int i = 0; i < 3; ++i) {
        const auto centre = geo.pointAt(radius, geo.angleAt(at[i]));
        const auto box = juce::Rectangle<float>(textHeight * 2.4f, textHeight * 1.3f).withCentre(centre);
        g.drawText(geo.bipolar() ? bipolarLabels[i] : labels[i], box, juce::Justification::centred, false);
    }
}

// The skirt: a black disc with sixty lighter ridges, darkened towards its rim, a top-lit edge.
void drawSkirt(juce::Graphics& g, const Geo& geo, float radius) {
    fillDiscShadow(g, geo.centre.translated(0.0f, geo.size * 0.027f), radius, geo.size * 0.05f, 0.75f);
    const auto skirt = geo.discBounds(radius);
    g.setColour(juce::Colour(0xff17181a));
    g.fillEllipse(skirt);

    constexpr int kRidges = 60;
    const float ridge = juce::MathConstants<float>::twoPi / (float)kRidges;
    juce::Path ridges;
    for (int i = 0; i < kRidges; ++i) {
        const float from = (float)i * ridge + ridge * 0.5f;
        ridges.addPieSegment(skirt, from, from + ridge * 0.25f, 0.0f);
    }
    g.setColour(juce::Colour(0xff34363a));
    g.fillPath(ridges);

    juce::ColourGradient rim(juce::Colours::transparentBlack, geo.centre, juce::Colours::black.withAlpha(0.35f),
                             geo.centre.translated(radius, 0.0f), true);
    rim.addColour(0.58, juce::Colours::transparentBlack);
    g.setGradientFill(rim);
    g.fillEllipse(skirt);

    juce::ColourGradient edge(juce::Colours::white.withAlpha(0.18f), skirt.getCentreX(), skirt.getY(),
                              juce::Colours::black.withAlpha(0.5f), skirt.getCentreX(), skirt.getBottom(), false);
    g.setGradientFill(edge);
    g.drawEllipse(skirt.reduced(0.5f), 1.0f);
}

// The cap: brushed metal (a fixed conic sweep of greys, so the light does not turn with the knob) with
// a soft top-left highlight and a dark hairline edge.
void drawCap(juce::Graphics& g, const Geo& geo, float radius) {
    static const juce::uint32 kBrushed[] = {0xffc9ccd0, 0xff8a8f96, 0xffe6e8ea, 0xff7b8088, 0xffc9ccd0,
                                            0xff949aa1, 0xffeceef0, 0xff80858c, 0xffc9ccd0};
    constexpr int kStops = (int)(sizeof(kBrushed) / sizeof(kBrushed[0]));
    constexpr int kSegments = 64;
    const auto cap = geo.discBounds(radius);
    const float start = juce::degreesToRadians(20.0f);
    const float step = juce::MathConstants<float>::twoPi / (float)kSegments;
    for (int i = 0; i < kSegments; ++i) {
        const float t = ((float)i + 0.5f) / (float)kSegments * (float)(kStops - 1);
        const int a = juce::jmin(kStops - 2, (int)t);
        const auto colour = juce::Colour(kBrushed[a]).interpolatedWith(juce::Colour(kBrushed[a + 1]), t - (float)a);
        juce::Path wedge;
        wedge.addPieSegment(cap, start + (float)i * step, start + (float)(i + 1) * step + 0.01f, 0.0f);
        g.setColour(colour);
        g.fillPath(wedge);
    }

    const juce::Point<float> focal(cap.getX() + cap.getWidth() * 0.34f, cap.getY() + cap.getHeight() * 0.28f);
    juce::ColourGradient shine(juce::Colours::white.withAlpha(0.55f), focal, juce::Colours::transparentWhite,
                               focal.translated(cap.getWidth() * 0.55f, 0.0f), true);
    g.setGradientFill(shine);
    g.fillEllipse(cap);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawEllipse(cap, 1.0f);
}

void drawLine(juce::Graphics& g, const Geo& geo, float skirtRadius) {
    juce::Path line;
    line.startNewSubPath(geo.pointAt(skirtRadius * 0.17f, geo.valueAngle));
    line.lineTo(geo.pointAt(skirtRadius * 0.95f, geo.valueAngle));
    const float width = juce::jmax(1.6f, geo.size * 0.026f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.strokePath(line, roundedStroke(width + 1.0f));
    g.setColour(juce::Colour(0xfff7f5ee));
    g.strokePath(line, roundedStroke(width));
}

} // namespace

void paintAnalog(juce::Graphics& g, const Geo& geo, const Theme& theme) {
    const bool numbers = geo.size >= kAnalogNumbersMinSize;
    const float dial = dialRadius(geo, numbers);
    const float skirt = dial - 7.0f * juce::jlimit(0.8f, 1.0f, geo.size / 54.0f);
    drawTicks(g, geo, theme, dial);
    if (numbers)
        drawNumbers(g, geo, theme, dial);
    drawSkirt(g, geo, skirt);
    drawCap(g, geo, skirt * 0.62f);
    drawLine(g, geo, skirt);
}

} // namespace synth::theme::knobs
