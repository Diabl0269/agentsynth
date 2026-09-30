// MacroNesting.cpp
//
// Nested-macro queries shared by the macro painters, MacroGroupController and GraphEditor's
// selection drag. Declared in MacroNesting.h.

#include "MacroNesting.h"

#include <algorithm>

namespace macro_nesting {

// Painters draw in this order so a child's hull, chip and strips land on top of its parent's.
// stable_sort keeps a flat (all depth 0) set in exactly the stored order it was always painted in.
std::vector<const synth::Macro*> macrosParentsFirst(const synth::MacroSet& macros) {
    std::vector<std::pair<int, const synth::Macro*>> byDepth;
    for (const auto& macro : macros.getAll())
        byDepth.emplace_back(macros.depth(macro.id), &macro);
    std::stable_sort(byDepth.begin(), byDepth.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<const synth::Macro*> out;
    out.reserve(byDepth.size());
    for (const auto& entry : byDepth)
        out.push_back(entry.second);
    return out;
}

// MacroSet::descendantMembers answers membership but is a uuid-sorted set; anything that lists or
// fans out over a macro's modules uses this instead, so a flat macro keeps its stored member order.
std::vector<juce::String> orderedDescendantMembers(const synth::MacroSet& macros, const juce::String& macroId) {
    std::vector<juce::String> out;
    const auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return out;
    out = macro->members;
    for (const auto& childId : macros.childrenOf(macroId)) {
        const auto childMembers = orderedDescendantMembers(macros, childId);
        out.insert(out.end(), childMembers.begin(), childMembers.end());
    }
    return out;
}

namespace {
bool allMoved(const std::set<juce::String>& uuids, const std::set<juce::String>& movedUuids) {
    return !uuids.empty() && std::all_of(uuids.begin(), uuids.end(),
                                         [&](const juce::String& uuid) { return movedUuids.count(uuid) > 0; });
}
} // namespace

// A collapsed macro's card is its own component, so a selection drag that moves its (hidden)
// members leaves the card and its persisted `bounds` behind; this names the ones that must follow.
// Carried = collapsed, every descendant member moved, AND its parent's every descendant member moved
// too — i.e. it rides inside a container that is moving as a whole. The parent condition is what
// keeps flat behaviour identical: a top-level card being dragged has no parent (its own drag writes
// its bounds), and a nested child card dragged on its own selects only its members, not its parent's.
std::vector<juce::String> collapsedMacrosCarriedBy(const synth::MacroSet& macros,
                                                   const std::set<juce::String>& movedUuids) {
    std::vector<juce::String> out;
    for (const auto& macro : macros.getAll()) {
        if (!macro.collapsed || macro.parentId.isEmpty())
            continue;
        if (allMoved(macros.descendantMembers(macro.parentId), movedUuids) &&
            allMoved(macros.descendantMembers(macro.id), movedUuids))
            out.push_back(macro.id);
    }
    return out;
}

std::vector<juce::String> ownerChain(const synth::MacroSet& macros, const juce::String& uuid) {
    const auto* owner = macros.findByMember(uuid);
    if (owner == nullptr)
        return {};
    auto chain = macros.ancestorChain(owner->id);
    chain.insert(chain.begin(), owner->id);
    return chain;
}

std::vector<juce::String> boundariesCrossed(const synth::MacroSet& macros, const juce::String& uuid,
                                            const std::vector<juce::String>& otherChain) {
    auto chain = ownerChain(macros, uuid);
    if (const auto* owner = macros.findByMember(uuid); owner != nullptr && owner->memberIsPort(uuid))
        chain.erase(chain.begin());
    chain.erase(std::remove_if(chain.begin(), chain.end(),
                               [&](const juce::String& id) {
                                   return std::find(otherChain.begin(), otherChain.end(), id) != otherChain.end();
                               }),
                chain.end());
    return chain;
}

bool isEmptyMacro(const synth::MacroSet& macros, const juce::String& macroId) {
    const auto* macro = macros.find(macroId);
    return macro != nullptr && macro->members.empty() && macros.childrenOf(macroId).empty();
}

} // namespace macro_nesting
