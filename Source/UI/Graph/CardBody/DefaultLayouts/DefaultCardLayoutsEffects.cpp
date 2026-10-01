// DefaultCardLayoutsEffects.cpp -- the code-default card layouts of Delay, Reverb, Chorus, Phaser, Flanger, Distortion,
// Bitcrusher, Ring Modulator and Pitch Shifter. Each type registers with defaults.add(type, layout, revision,
// dimRules), built with the cardlayout:: helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic
// layout. docs/layout/module-card-layout.md#default-layouts.
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

void registerEffectCardLayouts(DefaultCardLayouts& defaults) { juce::ignoreUnused(defaults); }

} // namespace synth
