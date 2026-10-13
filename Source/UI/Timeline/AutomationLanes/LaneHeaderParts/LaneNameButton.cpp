// Concern: the lane header's name button -- its paint (a border on hover, the shared accent ring on focus) and its
// keys.
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneNameButton.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

LaneNameButton::LaneNameButton()
    : juce::Button("laneName") {
    setComponentID("laneNameButton");
    setWantsKeyboardFocus(true);
    // The header owns the mouse (a drag from the name reorders the lane), so a click is never this button's own.
    setInterceptsMouseClicks(false, false);
    setMouseClickGrabsKeyboardFocus(false);
    setClickingTogglesState(false);
}

void LaneNameButton::setName(const juce::String& text) {
    if (text_ == text)
        return;
    text_ = text;
    repaint();
}

void LaneNameButton::setActionText(const juce::String& accessibleName, const juce::String& tooltip) {
    setTitle(accessibleName);
    setTooltip(tooltip);
}

void LaneNameButton::setBold(bool bold) {
    bold_ = bold;
    repaint();
}

juce::Font LaneNameButton::font() const {
    return juce::Font(juce::FontOptions(kFontSize, bold_ ? juce::Font::bold : juce::Font::plain));
}

int LaneNameButton::getPreferredWidth() const {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font(), text_, 0.0f, 0.0f);
    return (int)std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()) + 2 * kPadding;
}

void LaneNameButton::setHovered(bool hovered) {
    if (hovered_ == hovered)
        return;
    hovered_ = hovered;
    hoverFade_.setShown(hovered);
}

void LaneNameButton::paintButton(juce::Graphics& g, bool, bool) {
    const auto& colours = synth::theme::themeOf(*this).colors;
    const auto bounds = getLocalBounds().toFloat();
    if (hoverFade_.value() > 0.0f) {
        g.setColour(colours.accent.withMultipliedAlpha(hoverFade_.value()));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
    }
    g.setColour(colours.textPrimary);
    g.setFont(font());
    g.drawText(text_, getLocalBounds().reduced(kPadding, 0), juce::Justification::centredLeft, true);
    synth::ui::paintFocusRing(g, bounds, *this, 4.0f);
}

// Return and Space press at once, like the fold arrow: juce::Button posts Return's click to the message queue and has
// no Space handling. Every other key (Up/Down, Cmd+Alt+Up/Down) bubbles to the header.
bool LaneNameButton::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().testFlags(juce::ModifierKeys::allKeyboardModifiers))
        return false;
    if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey)) {
        if (isEnabled() && onClick)
            onClick();
        return true;
    }
    return false;
}

} // namespace synth::ui
