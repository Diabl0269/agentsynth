// Concern: the track header's mono/poly voice toggle (one icon button).
#include "UI/Timeline/TrackVoiceToggle.h"

#include "UI/Layout/ActionGlyphs.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kVoiceGlyphSize = 14.0f;
} // namespace

TrackVoiceToggle::TrackVoiceToggle() {
    setComponentID("trackVoiceToggle");
    setToggleable(true);
    onClick = [this] { press(); };
    setState(TrackPolyState::mono, {});
}

void TrackVoiceToggle::press() {
    if (onSwitch) // mono -> poly, poly -> mono, half poly -> poly
        onSwitch(state_ != TrackPolyState::poly);
}

void TrackVoiceToggle::setState(TrackPolyState state, const juce::String& trackName, const juce::String& shortcut) {
    state_ = state;
    const juce::String name = trackName.isNotEmpty() ? trackName : juce::String("track");
    setToggleState(state == TrackPolyState::poly, juce::dontSendNotification);
    juce::String tooltip;
    switch (state) {
    case TrackPolyState::poly:
        tooltip = "Poly: " + name + " plays several notes at once. Click to switch its whole voice chain to mono.";
        break;
    case TrackPolyState::mixed:
        tooltip = "Some of " + name +
                  "'s modules are mono and some poly right now. Click to switch its whole voice chain to poly.";
        break;
    default:
        tooltip = "Mono: " + name + " plays one note at a time. Click to switch its whole voice chain to poly.";
        break;
    }
    setTexts("Poly voices, " + name, formatShortcutHint(tooltip, shortcut));
}

// A note for mono, a chord for poly, an outlined chord for half and half; poly is lit in the accent so the state
// reads without hovering.
void TrackVoiceToggle::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto bounds = getLocalBounds().toFloat();
    const auto& colors = synth::theme::themeOf(*this).colors;
    if (highlighted || down) {
        g.setColour(colors.textPrimary.withAlpha(down ? 0.18f : 0.1f));
        g.fillRoundedRectangle(bounds, 3.0f);
    }
    const auto ink = highlighted                      ? colors.textPrimary
                     : state_ == TrackPolyState::mono ? colors.textMuted
                                                      : colors.accent;
    const auto voice = state_ == TrackPolyState::poly    ? VoiceGlyph::poly
                       : state_ == TrackPolyState::mixed ? VoiceGlyph::mixed
                                                         : VoiceGlyph::mono;
    // The glyph shrinks with a row too short for it, so it is never clipped.
    const float size = juce::jmin(kVoiceGlyphSize, bounds.getHeight());
    paintVoiceGlyph(g, juce::Rectangle<float>(size, size).withCentre(bounds.getCentre()), ink, voice);
    synth::ui::paintFocusRing(g, bounds, *this, 3.0f);
}

} // namespace synth::ui
