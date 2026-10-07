// Concern: resolving a node to the channel macro it belongs to when macros nest (see the header).
#include "Mixer/ChannelMacroLookup.h"

#include "MacroSet.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/ChannelStripModule.h"

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

// The owner map keeps the FIRST macro holding each uuid, as MacroSet::findByMember answers; a macro is a channel when
// one of its members is a Channel Strip node, as isChannelMacro decides by scanning the graph per call (a scan per
// mixer column made every snapshot O(columns x nodes)).
ChannelMacroIndex::ChannelMacroIndex(juce::AudioProcessorGraph& graph, const MacroSet& macros)
    : macros_(macros) {
    static const juce::Identifier uuidKey("uuid");
    std::set<juce::String> stripUuids;
    for (auto* node : graph.getNodes())
        if (const auto uuid = node->properties[uuidKey].toString();
            uuid.isNotEmpty() && dynamic_cast<ChannelStripModule*>(node->getProcessor()) != nullptr)
            stripUuids.insert(uuid);
    for (const auto& macro : macros.getAll())
        for (const auto& member : macro.members) {
            ownerByMember_.emplace(member, &macro);
            if (stripUuids.count(member) != 0)
                channelMacroIds_.insert(macro.id);
        }
}

bool ChannelMacroIndex::isChannel(const Macro& macro) const { return channelMacroIds_.count(macro.id) != 0; }

const Macro* ChannelMacroIndex::nearest(const juce::String& uuid) const {
    const auto found = uuid.isNotEmpty() ? ownerByMember_.find(uuid) : ownerByMember_.end();
    if (found == ownerByMember_.end())
        return nullptr;
    const Macro* owner = found->second;
    if (isChannel(*owner))
        return owner;
    for (const auto& id : macros_.ancestorChain(owner->id))
        if (const auto* ancestor = macros_.find(id); ancestor != nullptr && isChannel(*ancestor))
            return ancestor;
    return owner;
}

} // namespace synth
