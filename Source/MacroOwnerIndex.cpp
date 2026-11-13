// Concern: MacroOwnerIndex (MacroOwnerIndex.h) -- a pass's member lookups without a search of every macro per node.
#include "MacroOwnerIndex.h"

namespace synth {

// The first macro holding each uuid wins, as MacroSet::findByMember answers.
MacroOwnerIndex::MacroOwnerIndex(const MacroSet& macros)
    : macros_(macros) {
    for (const auto& macro : macros.getAll())
        for (const auto& member : macro.members)
            ownerByMember_.emplace(member, &macro);
}

const Macro* MacroOwnerIndex::ownerOf(const juce::String& uuid) const {
    const auto found = ownerByMember_.find(uuid);
    return found != ownerByMember_.end() ? found->second : nullptr;
}

// outermostCollapsedAncestorOf is non-empty exactly when the owner or one of its ancestors is collapsed, which is
// MacroSet::isEffectivelyCollapsed of the owner: answered once per owner.
bool MacroOwnerIndex::hiddenByCollapse(const juce::String& uuid) const {
    const auto* owner = ownerOf(uuid);
    if (owner == nullptr)
        return false;
    const auto [it, added] = collapsedByMacro_.emplace(owner->id, false);
    if (added)
        it->second = macros_.isEffectivelyCollapsed(owner->id);
    return it->second;
}

} // namespace synth
