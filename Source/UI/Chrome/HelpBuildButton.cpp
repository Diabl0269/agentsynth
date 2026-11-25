#include "HelpBuildButton.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kBtnFontHeight = 14.0f;
constexpr int kBtnPadLeft = 14;
constexpr int kBtnPadRight = 14;
constexpr int kBtnHeartGap = 8;
constexpr float kBtnHeartSize = 14.0f;
constexpr float kBtnCorner = 9.0f;
constexpr float kBtnBorder = 2.0f;
constexpr int kBtnBorderSamples = 72;
constexpr float kBtnPi2 = 6.2831853f;

// Phase helpers: the strip's 12 s cycle holds a whole number of every effect's period.
constexpr float kBtnFillLoops = 2.0f;    // 6 s
constexpr float kBtnSpinLoops = 3.0f;    // 4 s
constexpr float kBtnShimmerLoops = 3.0f; // 4 s, resting for its first 55 percent
constexpr float kBtnBeatLoops = 8.0f;    // 1.5 s

float fracBtn(float v) { return v - std::floor(v); }

juce::Colour mixBtn(juce::Colour a, juce::Colour b, float t) {
    return a.interpolatedWith(b, juce::jlimit(0.0f, 1.0f, t));
}

// The beat: a quick double pulse in the first 40 percent of the period, then rest.
float heartScaleBtn(float beat01) {
    const float t = beat01;
    if (t < 0.4f)
        return 1.0f;
    if (t < 0.5f)
        return 1.0f + 0.25f * std::sin((t - 0.4f) / 0.1f * 3.1415927f * 0.5f);
    if (t < 0.6f)
        return 1.25f - 0.25f * ((t - 0.5f) / 0.1f);
    if (t < 0.75f)
        return 1.0f + 0.15f * std::sin((t - 0.6f) / 0.15f * 3.1415927f);
    return 1.0f;
}
} // namespace

juce::Colour HelpBuildButton::inkColour(const synth::theme::Colors& c) {
    const auto fill = mixBtn(c.accent, c.hueRose, 0.5f);
    return fill.getPerceivedBrightness() > 0.55f ? c.iconInk : c.iconPaper;
}

HelpBuildButton::HelpBuildButton()
    : juce::Button("Help build Agent Synth") {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(true);
}

int HelpBuildButton::preferredWidth(const juce::String& text) {
    const int textWidth =
        juce::GlyphArrangement::getStringWidthInt(synth::theme::AppLookAndFeel::uiSemiBoldFont(kBtnFontHeight), text);
    return kBtnPadLeft + textWidth + kBtnHeartGap + (int)kBtnHeartSize + kBtnPadRight;
}

void HelpBuildButton::setPhase(float phase) {
    phase_ = phase;
    repaint();
}

void HelpBuildButton::setMotion(bool moving) {
    if (moving_ == moving)
        return;
    moving_ = moving;
    if (!moving_)
        phase_ = 0.0f;
    repaint();
}

void HelpBuildButton::resized() {
    juce::Path ring;
    ring.addRoundedRectangle(getLocalBounds().toFloat().reduced(kBtnBorder * 0.5f), kBtnCorner);
    borderPoints_.clear();
    const float length = ring.getLength();
    for (int i = 0; i <= kBtnBorderSamples; ++i)
        borderPoints_.push_back(ring.getPointAlongPath(length * (float)i / (float)kBtnBorderSamples));
}

void HelpBuildButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto& c = theme::themeOf(*this).colors;
    const auto bounds = getLocalBounds().toFloat();
    const auto body = bounds.reduced(kBtnBorder * 0.5f);
    const float phase = moving_ ? phase_ : 0.0f;

    // The fill: accent to rose and back, sliding along the button once per fill loop.
    const float slide = fracBtn(phase * kBtnFillLoops);
    juce::ColourGradient fill(c.accent, bounds.getX(), 0.0f, c.accent, bounds.getRight(), 0.0f, false);
    for (int i = 0; i <= 8; ++i) {
        const float pos = (float)i / 8.0f;
        const float wave = 0.5f - 0.5f * std::cos(kBtnPi2 * (pos * 0.5f + slide));
        fill.addColour((double)pos, mixBtn(c.accent, c.hueRose, wave));
    }
    g.setGradientFill(fill);
    g.fillRoundedRectangle(body, kBtnCorner - 1.0f);
    if (down)
        g.setColour(juce::Colours::black.withAlpha(0.14f));
    else if (highlighted)
        g.setColour(juce::Colours::white.withAlpha(0.14f));
    if (down || highlighted)
        g.fillRoundedRectangle(body, kBtnCorner - 1.0f);

    // The shimmer: a skewed light band that rests, then crosses the button.
    if (moving_) {
        const float s = fracBtn(phase * kBtnShimmerLoops);
        if (s > 0.55f) {
            const float t = (s - 0.55f) / 0.45f;
            const float x = -40.0f + t * (bounds.getWidth() + 80.0f);
            juce::Path band;
            band.addQuadrilateral(x + 8.0f, 0.0f, x + 44.0f, 0.0f, x + 36.0f, bounds.getHeight(), x,
                                  bounds.getHeight());
            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle(body, kBtnCorner - 1.0f);
            g.reduceClipRegion(clip, {});
            auto shine = juce::ColourGradient(juce::Colours::white.withAlpha(0.0f), x + 8.0f, 0.0f,
                                              juce::Colours::white.withAlpha(0.0f), x + 44.0f, 0.0f, false);
            shine.addColour(0.5, juce::Colours::white.withAlpha(0.55f));
            g.setGradientFill(shine);
            g.fillPath(band);
            g.restoreState();
        }
    }

    // The border light: accent through rose to white, once round the button per spin loop.
    const auto centre = bounds.getCentre();
    const float spin = fracBtn(phase * kBtnSpinLoops);
    for (size_t i = 0; i + 1 < borderPoints_.size(); ++i) {
        const auto mid = (borderPoints_[i] + borderPoints_[i + 1]) * 0.5f;
        const float angle = fracBtn(std::atan2(mid.y - centre.y, mid.x - centre.x) / kBtnPi2 + spin);
        const float w = 0.5f - 0.5f * std::cos(kBtnPi2 * angle);
        const auto colour = w < 0.5f ? mixBtn(c.accent, c.hueRose, w * 2.0f)
                                     : mixBtn(c.hueRose, juce::Colours::white, (w - 0.5f) * 2.0f);
        g.setColour(colour);
        g.drawLine({borderPoints_[i], borderPoints_[i + 1]}, kBtnBorder);
    }

    // The label and the heart.
    const auto ink = inkColour(c);
    g.setColour(ink);
    g.setFont(synth::theme::AppLookAndFeel::uiSemiBoldFont(kBtnFontHeight));
    const int heartRoom = (int)kBtnHeartSize + kBtnHeartGap + kBtnPadRight;
    g.drawText(getButtonText(), getLocalBounds().withTrimmedLeft(kBtnPadLeft).withTrimmedRight(heartRoom),
               juce::Justification::centredLeft, false);

    static const juce::Path heart =
        juce::Drawable::parseSVGPath("M10 17.2 3.2 10.6A4.3 4.3 0 0 1 10 5a4.3 4.3 0 0 1 6.8 5.6Z");
    const float scale = moving_ ? heartScaleBtn(fracBtn(phase * kBtnBeatLoops)) : 1.0f;
    const auto heartBox =
        juce::Rectangle<float>(kBtnHeartSize, kBtnHeartSize)
            .withCentre({bounds.getRight() - kBtnPadRight - kBtnHeartSize * 0.5f, bounds.getCentreY()});
    auto place = juce::RectanglePlacement(juce::RectanglePlacement::centred)
                     .getTransformToFit(heart.getBounds(),
                                        heartBox.withSizeKeepingCentre(kBtnHeartSize * scale, kBtnHeartSize * scale));
    g.fillPath(heart, place);

    paintFocusRing(g, bounds, *this, kBtnCorner);
}

} // namespace synth::ui
