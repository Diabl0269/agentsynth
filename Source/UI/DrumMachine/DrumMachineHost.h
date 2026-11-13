#pragma once

#include "DrumMachineComponent.h"
#include "UI/Layout/FadeVisibility.h"

namespace synth::ui {

// The drum machine with the fade that cross-fades it against the clip lanes, held by TimelinePanelComponent behind one
// pointer so the panel's header does not include either.
struct DrumMachineHost {
    DrumMachineComponent grid;
    FadeVisibility fade{&grid};
    synth::TrackId previewTrack; // the track a running Play clip loop was started on (the clip may be gone by the stop)
};

} // namespace synth::ui
