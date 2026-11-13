#pragma once

#include "MacroSet.h"
#include <map>

namespace synth {

/** MacroSet's per-node questions answered from one member map, for a pass that asks them of every node
 *  (docs/architecture/graph-queries.md). A snapshot: `macros` must not change while it is used, and must outlive it. */
class MacroOwnerIndex {
public:
    explicit MacroOwnerIndex(const MacroSet& macros);
    /** The same answer as macros.findByMember(uuid). */
    const Macro* ownerOf(const juce::String& uuid) const;
    /** True when macros.outermostCollapsedAncestorOf(uuid) is non-empty. */
    bool hiddenByCollapse(const juce::String& uuid) const;

private:
    const MacroSet& macros_;
    std::map<juce::String, const Macro*> ownerByMember_;
    mutable std::map<juce::String, bool> collapsedByMacro_;
};

} // namespace synth
