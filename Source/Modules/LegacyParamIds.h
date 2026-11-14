// Concern: parameter ids a module dropped, for the places that store a parameter by id outside the node's own
// params (automation lanes, MIDI Remote bindings). The node's params go through ModuleBase::adoptLegacyParams.
#pragma once

#include <juce_core/juce_core.h>

namespace synth {

/** The id a saved binding should use now. The Reverb's Wet knob became Mix, so a lane or a controller
    binding on "wet" follows the Mix. A binding on "dry" has no counterpart and is left as it is, a binding to a
    parameter the module no longer has, which every surface already treats as dangling. Only the Reverb ever had a
    "wet" parameter, so the id alone is enough. */
inline juce::String currentParamId(const juce::String& savedId) {
    return savedId == "wet" ? juce::String("mix") : savedId;
}

} // namespace synth
