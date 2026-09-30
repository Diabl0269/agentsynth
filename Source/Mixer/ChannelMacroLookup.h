#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

class MacroSet;
struct Macro;

/** The macro a node belongs to AS A MIXER CHANNEL: the nearest macro in the node's owner chain
 *  (its innermost owner first, then outward) that is a channel macro (isChannelMacro), else its
 *  innermost owner, else nullptr when the node is in no macro. Flat sets always resolve to the
 *  innermost owner, so this equals MacroSet::findByMember there. Use it wherever the caller means
 *  "the channel this node is in" rather than "the innermost group around it". */
const Macro* nearestChannelMacro(juce::AudioProcessorGraph& graph, const MacroSet& macros, const juce::String& uuid);

} // namespace synth
