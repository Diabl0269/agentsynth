// Concern: RoutingComboButton and RoutingTextLink -- painting and focus behaviour.
#include "TimelineRoutingPaneControls.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
const synth::theme::Colors* colorsOf(const juce::Component& component) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&component.getLookAndFeel());
    return laf != nullptr ? &laf->getTheme().colors : nullptr;
}

// A click on either control must never move keyboard focus off the panel's focus root.
void optOutOfFocus(juce::Button& button) {
    button.setWantsKeyboardFocus(false);
    button.setMouseClickGrabsKeyboardFocus(false);
}
} // namespace

RoutingComboButton::RoutingComboButton(const juce::String& name)
    : juce::Button(name) {
    optOutOfFocus(*this);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void RoutingComboButton::setWarning(bool warning) {
    if (warning_ == warning)
        return;
    warning_ = warning;
    repaint();
}

void RoutingComboButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto* colors = colorsOf(*this);
    const auto surface = colors != nullptr ? colors->surface : juce::Colour(0xff1B1F26);
    const auto border = colors != nullptr ? colors->border : juce::Colour(0xff2A2F38);
    const auto text = colors != nullptr ? colors->textPrimary : juce::Colour(0xffEAEEF3);
    const auto muted = colors != nullptr ? colors->textMuted : juce::Colour(0xff8A93A0);
    const auto warning = colors != nullptr ? colors->warning : juce::Colour(0xffE0A33D);

    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(down ? surface.brighter(0.25f) : (highlighted ? surface.brighter(0.12f) : surface));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(warning_ ? warning.withAlpha(0.7f) : border);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    auto area = getLocalBounds().reduced(7, 0);
    auto chevron = area.removeFromRight(10).toFloat();
    juce::Path arrow;
    arrow.addTriangle(chevron.getX(), chevron.getCentreY() - 2.0f, chevron.getRight() - 2.0f,
                      chevron.getCentreY() - 2.0f, chevron.getCentreX() - 1.0f, chevron.getCentreY() + 2.5f);
    g.setColour(muted);
    g.fillPath(arrow);

    area.removeFromRight(4);
    g.setColour(warning_ ? warning : text);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText(getButtonText(), area, juce::Justification::centredLeft, true);
}

RoutingTextLink::RoutingTextLink(const juce::String& text)
    : juce::Button(text) {
    optOutOfFocus(*this);
    setTitle(text);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void RoutingTextLink::paintButton(juce::Graphics& g, bool highlighted, bool) {
    const auto* colors = colorsOf(*this);
    const auto accent = colors != nullptr ? colors->accent : juce::Colour(0xff00D1FF);
    const juce::Font font{juce::FontOptions(11.0f)};
    g.setColour(highlighted ? accent.brighter(0.3f) : accent);
    g.setFont(font);
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centredLeft, false);
    if (highlighted) {
        const float width = font.getStringWidthFloat(getButtonText());
        g.fillRect(0.0f, static_cast<float>(getHeight() / 2 + 7), width, 1.0f);
    }
}

} // namespace synth::ui
