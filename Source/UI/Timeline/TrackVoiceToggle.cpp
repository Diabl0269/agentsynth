// Concern: the track header's mono/poly voice toggle (one icon button).
#include "UI/Timeline/TrackVoiceToggle.h"

#include "UI/Layout/ActionGlyphs.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kGlyphSize = 14.0f;
} // namespace

TrackVoiceToggle::TrackVoiceToggle() {
    setComponentID("trackVoiceToggle");
    setToggleable(true);
    onClick = [this] {
        if (onSwitch) // mono -> poly, poly -> mono, half poly -> poly
            onSwitch(state_ != TrackPolyState::poly);
    };
    setState(TrackPolyState::mono, {});
}

void TrackVoiceToggle::setState(TrackPolyState state, const juce::String& trackName) {
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
    setTexts("Poly voices, " + name, tooltip);
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
    paintVoiceGlyph(g, juce::Rectangle<float>(kGlyphSize, kGlyphSize).withCentre(bounds.getCentre()), ink, voice);
    synth::ui::paintFocusRing(g, bounds, *this, 3.0f);
}

} // namespace synth::ui
