// Concern: resolving a node to the channel macro it belongs to when macros nest (see the header).
#include "Mixer/ChannelMacroLookup.h"

#include "MacroSet.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"

namespace synth {

const Macro* nearestChannelMacro(juce::AudioProcessorGraph& graph, const MacroSet& macros, const juce::String& uuid) {
    const Macro* owner = uuid.isNotEmpty() ? macros.findByMember(uuid) : nullptr;
    if (owner == nullptr)
        return nullptr;
    if (isChannelMacro(*owner, graph))
        return owner;
    for (const auto& id : macros.ancestorChain(owner->id))
        if (const auto* ancestor = macros.find(id); ancestor != nullptr && isChannelMacro(*ancestor, graph))
            return ancestor;
    return owner;
}

} // namespace synth
