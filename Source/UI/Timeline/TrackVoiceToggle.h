#pragma once

#include "UI/Timeline/TrackPolyState.h"
#include "UI/Timeline/TrackShowModuleButton.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The track header's voice toggle (docs/timeline/tracks.md#the-monopoly-switch): ONE icon button, a note head for
// mono and a stack of three for poly (outlined while a track is half poly). It only reports a press; the host flips
// the track's whole voice graph and the header then shows what the graph says, so the toggle state is set from the
// graph and never by the click itself. A press on a half-poly track asks for poly. It is a TrackShowModuleButton in
// size and behaviour (square, a Tab stop with Return/Space, a click never pulls focus off the row), so it fits the
// bottom row wherever the show-module button does.
class TrackVoiceToggle : public TrackShowModuleButton {
public:
    TrackVoiceToggle();

    /** Called with true for Poly, false for Mono. */
    std::function<void(bool poly)> onSwitch;

    /** What a click does, at once: asks for the other mode (half poly asks for poly). The P key calls this. */
    void press();

    /** Shows `state` (glyph, toggle state on = poly, tooltip ending in the `shortcut` key text) for `trackName` (the
     *  screen-reader name). */
    void setState(TrackPolyState state, const juce::String& trackName, const juce::String& shortcut = {});
    TrackPolyState getState() const noexcept { return state_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    TrackPolyState state_ = TrackPolyState::mono;
};

} // namespace synth::ui
