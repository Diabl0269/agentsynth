#pragma once

// One learnable control a surface offers the pick-target overlay (docs/control/midi-remote-ui.md
// #assign-from-the-panel-control-first-learn). Each surface (module card, mixer column, master
// column, transport bar) keeps its own registry of what it can map and reports it through a
// collectPickCandidates() that appends these; the overlay never learns what a card is.

#include "MidiRemote/PickTarget.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

struct PickCandidate {
    juce::Component::SafePointer<juce::Component> component;
    synth::midi::PickTarget target;
};

} // namespace synth::ui
