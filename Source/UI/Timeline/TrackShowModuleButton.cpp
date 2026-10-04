// Concern: the track header's "Show module" button (the pointer and Tab twin of Ctrl+E).
#include "UI/Timeline/TrackShowModuleButton.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kGlyphSize = 12.0f;
constexpr float kRingThickness = 1.4f;
} // namespace

// Focusable for the keyboard path, but a click must not pull focus off the row: the row's own focus is what
// its keys and the panel's selected track follow (same reason as TrackFoldArrow).
TrackShowModuleButton::TrackShowModuleButton()
    : juce::Button("trackShowModuleButton") {
    setComponentID("trackShowModuleButton");
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setClickingTogglesState(false);
    setTrack({}, {});
}

void TrackShowModuleButton::setTrack(const juce::String& trackName, const juce::String& shortcutText) {
    const juce::String name = trackName.isNotEmpty() ? trackName : juce::String("track");
    setTitle("Show " + name + " module");
    setTooltip("Show " + name + "'s module on the canvas; an instrument's window opens, or closes if open (" +
               shortcutText + ")");
    repaint();
}

// A target: a ring with four ticks reaching out of it, drawn as paths in the theme's text colour so it follows
// every theme with no icon asset.
void TrackShowModuleButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto bounds = getLocalBounds().toFloat();
    const auto& colors = synth::theme::themeOf(*this).colors;
    if (highlighted || down) {
        g.setColour(colors.textPrimary.withAlpha(down ? 0.18f : 0.1f));
        g.fillRoundedRectangle(bounds, 3.0f);
    }
    const auto glyph = juce::Rectangle<float>(kGlyphSize, kGlyphSize).withCentre(bounds.getCentre());
    const auto centre = glyph.getCentre();
    const float radius = kGlyphSize * 0.28f;
    const float tick = kGlyphSize * 0.5f;
    g.setColour(highlighted ? colors.textPrimary : colors.textMuted);
    g.drawEllipse(glyph.withSizeKeepingCentre(radius * 2.0f, radius * 2.0f), kRingThickness);
    for (const auto d :
         {juce::Point<float>(1, 0), juce::Point<float>(-1, 0), juce::Point<float>(0, 1), juce::Point<float>(0, -1)})
        g.drawLine({centre + d * (radius + 1.0f), centre + d * tick}, kRingThickness);

    synth::ui::paintFocusRing(g, bounds, *this, 3.0f);
}

// Return and Space press at once, like TrackFoldArrow: juce::Button posts Return's click to the message queue and
// has no Space handling.
bool TrackShowModuleButton::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey)) {
        if (isEnabled() && onClick)
            onClick();
        return true;
    }
    return juce::Button::keyPressed(key);
}

} // namespace synth::ui
