// Concern: the track header's "Show module" button (the pointer and Tab twin of Ctrl+E).
#include "UI/Timeline/TrackShowModuleButton.h"

#include "UI/Layout/ActionGlyphs.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kShowGlyphSize = 12.0f;
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
    setTexts("Show " + name + " module",
             "Show " + name + "'s module on the canvas; an instrument's window opens, or closes if open (" +
                 shortcutText + ")");
}

void TrackShowModuleButton::setTexts(const juce::String& title, const juce::String& tooltip) {
    setTitle(title);
    setTooltip(tooltip);
    repaint();
}

// The shared show-on-canvas glyph (UI/Layout/ActionGlyphs.h), in the theme's text colour so it follows every theme
// with no icon asset.
void TrackShowModuleButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto bounds = getLocalBounds().toFloat();
    const auto& colors = synth::theme::themeOf(*this).colors;
    if (highlighted || down) {
        g.setColour(colors.textPrimary.withAlpha(down ? 0.18f : 0.1f));
        g.fillRoundedRectangle(bounds, 3.0f);
    }
    paintShowOnCanvasGlyph(g, juce::Rectangle<float>(kShowGlyphSize, kShowGlyphSize).withCentre(bounds.getCentre()),
                           highlighted ? colors.textPrimary : colors.textMuted);

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
