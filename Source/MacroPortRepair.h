#pragma once

#include "MacroSet.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/** True for a MacroInlet, MacroOutlet or their MIDI variants: the module types that front a macro port. */
bool isMacroPortModule(const juce::AudioProcessor* processor);

/** True when `uuid` names a port module no macro lists as one of its ports (a project can hold such a node: it
 *  carries signal but has no hull edge to sit on). */
bool isStrayMacroPort(const MacroSet& macros, const juce::String& uuid, const juce::AudioProcessor* processor);

/** Makes each stray port module whose cables meet members of exactly one macro that macro's port again, as the end of
 *  a load. A stray that meets no macro member, or members of several, is left alone. Returns how many were adopted. */
int adoptStrayMacroPorts(const juce::AudioProcessorGraph& graph, MacroSet& macros);

} // namespace synth
