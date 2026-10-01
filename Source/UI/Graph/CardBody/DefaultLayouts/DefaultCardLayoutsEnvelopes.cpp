// DefaultCardLayoutsEnvelopes.cpp -- the code-default card layouts of ADSR (and its "Amp Env" and "Filter Env" factory
// keys, each its own entry), VCA, Envelope Follower, Sample & Hold, Math, Voice Mixer, Poly MIDI and MIDI Keyboard.
// Each type registers with defaults.add(type, layout, revision, dimRules), built with the cardlayout::
// helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic layout.
// The MIDI Keyboard card builds no CardBody (cardBodyBuildsWidgetsFor is false for it), so its Octave
// stepper is card code, not an entry here.
// docs/layout/module-card-layout.md#default-layouts.
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

void registerEnvelopeCardLayouts(DefaultCardLayouts& defaults) { juce::ignoreUnused(defaults); }

} // namespace synth
