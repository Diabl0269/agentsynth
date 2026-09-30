// MacroNesting.h
//
// Pure MacroSet queries the canvas needs for nested macros (docs/layout/macro-cards.md#nested-macros):
// paint order, member lists in a stable order, and which collapsed macros a drag carries along.
// No GraphEditor or component dependency, so painters, the controller and the selection drag share
// one definition.
#pragma once

#include "MacroSet.h"

#include <set>
#include <vector>

namespace macro_nesting {

/** Every macro, parents before their children (stable by depth, so a flat set keeps stored order). */
std::vector<const synth::Macro*> macrosParentsFirst(const synth::MacroSet& macros);

/** `macroId`'s direct members in stored order, then each child's (recursively, in childrenOf order). */
std::vector<juce::String> orderedDescendantMembers(const synth::MacroSet& macros, const juce::String& macroId);

/** Ids of the collapsed macros a drag of `movedUuids` carries along: see the .cpp. */
std::vector<juce::String> collapsedMacrosCarriedBy(const synth::MacroSet& macros,
                                                   const std::set<juce::String>& movedUuids);

} // namespace macro_nesting
