#include "StopButton.h"

#include <cmath>

#include "UI/Layout/FocusRing.h"
#include "UI/Layout/TextLinkButton.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ReadableText.h"

namespace synth::ui {

namespace {
constexpr float kGlyphSize = 8.0f;
constexpr float kGlyphGap = 5.0f;
constexpr int kPadX = 10;
constexpr float kCorner = 4.0f;

float contentWidth(const juce::String& text) {
    return kGlyphSize + kGlyphGap + juce::GlyphArrangement::getStringWidth(textLinkFont(), text);
}
} // namespace

StopButton::StopButton(const juce::String& text)
    : juce::Button(text) {
    setTitle(text);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

int StopButton::getIdealWidth() const { return (int)std::ceil(contentWidth(getButtonText())) + 2 * kPadX; }

juce::Colour StopButton::toneFor(const synth::theme::Colors& c) {
    return synth::theme::nudgeToReadable(c.error, {c.bg0, c.bg1, c.surface});
}

void StopButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto& colours = synth::theme::themeOf(*this).colors;
    const auto tone = toneFor(colours).withMultipliedAlpha(isEnabled() ? 1.0f : 0.4f);
    const auto bounds = getLocalBounds().toFloat();
    const auto box = bounds.reduced(0.5f);

    if (highlighted || down) {
        g.setColour(tone.withMultipliedAlpha(down ? 0.22f : 0.12f));
        g.fillRoundedRectangle(box, kCorner);
    }
    g.setColour(tone);
    g.drawRoundedRectangle(box, kCorner, 1.0f);

    const float width = contentWidth(getButtonText());
    const float x = bounds.getCentreX() - width * 0.5f;
    g.fillRoundedRectangle(x, bounds.getCentreY() - kGlyphSize * 0.5f, kGlyphSize, kGlyphSize, 1.5f);
    g.setFont(textLinkFont());
    const auto textArea = juce::Rectangle<float>(x + kGlyphSize + kGlyphGap, bounds.getY(), width, bounds.getHeight());
    g.drawText(getButtonText(), textArea.getSmallestIntegerContainer(), juce::Justification::centredLeft, false);
    paintFocusRing(g, bounds, *this, kCorner);
}

} // namespace synth::ui
