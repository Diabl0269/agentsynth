// Badge.cpp -- the shared origin badge.
#include "UI/Chrome/Badge/Badge.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kFontSize = 10.5f;
constexpr int kSidePadding = 7;
constexpr float kFillAlpha = 0.16f;
constexpr float kBorderAlpha = 0.5f;
// How far the text moves from the badge's hue toward the theme's primary text: keeps it readable on a light theme,
// where a bright hue alone is too pale.
constexpr float kTextBlend = 0.45f;

juce::Font badgeFont() { return juce::Font(juce::FontOptions(kFontSize)); }

juce::Colour hueOf(const synth::theme::Colors& c, BadgeKind kind) {
    switch (kind) {
    case BadgeKind::Yours:
        return c.accent;
    case BadgeKind::Pack:
        return c.hueGreen;
    case BadgeKind::Marketplace:
        return c.hueAmber;
    case BadgeKind::Factory:
        break;
    }
    return c.textMuted;
}
} // namespace

const char* badgeText(BadgeKind kind) {
    switch (kind) {
    case BadgeKind::Yours:
        return "Yours";
    case BadgeKind::Factory:
        return "Factory";
    case BadgeKind::Pack:
        return "Pack";
    case BadgeKind::Marketplace:
        return "Marketplace";
    }
    return "";
}

int badgeWidth(BadgeKind kind) {
    return (int)std::ceil(juce::GlyphArrangement::getStringWidth(badgeFont(), badgeText(kind))) + 2 * kSidePadding;
}

BadgeColours badgeColours(const synth::theme::Theme& theme, BadgeKind kind) {
    const auto hue = hueOf(theme.colors, kind);
    return {hue.withAlpha(kFillAlpha), hue.withAlpha(kBorderAlpha),
            hue.interpolatedWith(theme.colors.textPrimary, kTextBlend)};
}

void paintBadge(juce::Graphics& g, const synth::theme::Theme& theme, BadgeKind kind, juce::Rectangle<float> area) {
    const auto colours = badgeColours(theme, kind);
    const float radius = area.getHeight() * 0.5f;
    g.setColour(colours.fill);
    g.fillRoundedRectangle(area, radius);
    g.setColour(colours.border);
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
    g.setColour(colours.text);
    g.setFont(badgeFont());
    g.drawText(badgeText(kind), area, juce::Justification::centred, false);
}

Badge::Badge(BadgeKind kind)
    : kind_(kind) {
    setInterceptsMouseClicks(false, false);
    setTitle(badgeText(kind_));
}

void Badge::setKind(BadgeKind kind) {
    if (kind == kind_)
        return;
    kind_ = kind;
    setTitle(badgeText(kind_));
    repaint();
}

juce::Rectangle<int> Badge::getIdealBounds() const { return {badgeWidth(kind_), kBadgeHeight}; }

void Badge::paint(juce::Graphics& g) { paintBadge(g, synth::theme::themeOf(*this), kind_, getLocalBounds().toFloat()); }

} // namespace synth::ui
