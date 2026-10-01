// DefaultCardLayoutsSources.cpp -- the code-default card layouts of Oscillator, Noise, Sampler, LFO and Wavetable.
// Each type registers with defaults.add(type, layout, revision, dimRules), built with the cardlayout::
// helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic layout.
// The Wavetable card is bespoke today (cardBodyLayoutIsDataDriven is false for it), so its entry
// only takes effect once its tab strip is drawn from `tab` sections.
// docs/layout/module-card-layout.md#default-layouts.
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

void registerSourceCardLayouts(DefaultCardLayouts& defaults) { juce::ignoreUnused(defaults); }

} // namespace synth
