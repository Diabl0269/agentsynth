// CardLayoutHideZone.cpp -- the "Drop to hide" area under the card while a control is being dragged: the
// outline's dashed accent stroke and wash, stronger while the pointer is over it.
#include "CardLayoutHideZone.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr float kCornerRadius = 7.0f;
constexpr float kIdleWash = 0.07f;
constexpr float kActiveWash = 0.22f;

} // namespace

CardLayoutHideZone::CardLayoutHideZone() {
    setInterceptsMouseClicks(false, false);
    setAccessible(false);
    setTitle("Drop to hide");
}

void CardLayoutHideZone::setActive(bool active) {
    if (active == active_)
        return;
    active_ = active;
    repaint();
}

void CardLayoutHideZone::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto accent = theme.colors.accent;
    const auto area = getLocalBounds().toFloat();
    g.setColour(theme.colors.surface);
    g.fillRoundedRectangle(area, kCornerRadius);
    g.setColour(accent.withAlpha(active_ ? kActiveWash : kIdleWash));
    g.fillRoundedRectangle(area, kCornerRadius);

    juce::Path outline;
    outline.addRoundedRectangle(area.reduced(0.5f), kCornerRadius);
    juce::Path dashed;
    const float dashes[] = {4.0f, 3.0f};
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
    g.setColour(accent.withAlpha(active_ ? 1.0f : 0.75f));
    g.fillPath(dashed);

    g.setColour(accent);
    g.setFont(juce::Font(juce::FontOptions(theme.type.label)));
    g.drawText("Drop to hide", getLocalBounds(), juce::Justification::centred);
}

} // namespace synth::ui
