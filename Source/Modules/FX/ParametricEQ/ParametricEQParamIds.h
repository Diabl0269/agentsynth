#pragma once

#include <juce_core/juce_core.h>

namespace synth::eq {

/** True for a Parametric EQ parameter no surface shows yet: bands 5-24 and every band's Type and Slope.
 *  The same answer as ParametricEQModule::isParameterExposed, for code that holds only a node's JSON. */
bool isHiddenParamId(const juce::String& paramId);

} // namespace synth::eq
