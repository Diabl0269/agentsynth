#pragma once

#include "UI/Timeline/TrackPolyState.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The track header's Mono | Poly switch (docs/timeline/tracks.md#the-monopoly-switch): two joined buttons, the
// current voice mode filled. It only reports a press; the host flips the track's whole voice graph and the header
// then shows what the graph says. Neither side is filled while a track is half poly, so the state is never guessed.
// Both sides are Tab stops (Return/Space press) that leave focus on the row when clicked, like the M/S/R toggles.
class TrackPolySwitch : public juce::Component {
public:
    static constexpr int kWidth = 54; // px, both sides

    TrackPolySwitch();

    /** Called with true for Poly, false for Mono. */
    std::function<void(bool poly)> onSwitch;

    /** Shows `state` (filled side, tooltips) for `trackName` (the screen-reader names). */
    void setState(TrackPolyState state, const juce::String& trackName);
    TrackPolyState getState() const noexcept { return state_; }

    juce::Button& getMonoButton() noexcept { return mono_; }
    juce::Button& getPolyButton() noexcept { return poly_; }

    void resized() override;

private:
    juce::TextButton mono_{"Mono"};
    juce::TextButton poly_{"Poly"};
    TrackPolyState state_ = TrackPolyState::none;
};

} // namespace synth::ui
