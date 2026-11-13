#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <set>

namespace synth {

class MacroSet;
struct Macro;

/** The macro a node belongs to AS A MIXER CHANNEL: the nearest macro in the node's owner chain
 *  (its innermost owner first, then outward) that is a channel macro (isChannelMacro), else its
 *  innermost owner, else nullptr when the node is in no macro. Flat sets always resolve to the
 *  innermost owner, so this equals MacroSet::findByMember there. Use it wherever the caller means
 *  "the channel this node is in" rather than "the innermost group around it". */
const Macro* nearestChannelMacro(juce::AudioProcessorGraph& graph, const MacroSet& macros, const juce::String& uuid);

/** nearestChannelMacro answered from lookups built once, for a pass that asks it for many nodes. A snapshot: the
 *  graph and `macros` must not change while it is used, and `macros` must outlive it. */
class ChannelMacroIndex {
public:
    ChannelMacroIndex(juce::AudioProcessorGraph& graph, const MacroSet& macros);
    /** The same answer as nearestChannelMacro(graph, macros, uuid). */
    const Macro* nearest(const juce::String& uuid) const;

private:
    bool isChannel(const Macro& macro) const;
    const MacroSet& macros_;
    std::map<juce::String, const Macro*> ownerByMember_;
    std::set<juce::String> channelMacroIds_;
};

} // namespace synth
