// DefaultCardLayoutsFilterDynamics.cpp -- the code-default card layouts of Filter, Compressor, Limiter and Gate.
// Each type registers with defaults.add(type, layout, revision, dimRules), built with the cardlayout::
// helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic layout.
// docs/layout/module-card-layout.md#default-layouts.
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

void registerFilterDynamicsCardLayouts(DefaultCardLayouts& defaults) { juce::ignoreUnused(defaults); }

} // namespace synth
