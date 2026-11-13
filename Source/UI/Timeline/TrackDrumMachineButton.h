#pragma once

#include "UI/Timeline/TrackShowModuleButton.h"

namespace synth::ui {

// The small step-grid glyph button on a Drum Kit track's header: opens the drum machine on the clip under the playhead.
// Everything but the glyph is TrackShowModuleButton's (a real Tab stop, Return/Space press it, a click never pulls
// focus off the row).
class TrackDrumMachineButton : public TrackShowModuleButton {
public:
    TrackDrumMachineButton();
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
};

} // namespace synth::ui
