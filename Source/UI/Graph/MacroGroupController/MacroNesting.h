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

/** The macros whose boundary a cable from node `uuid` to somewhere with owner chain `otherChain` crosses at
 *  `uuid`'s end, innermost first. `uuid`'s owner chain (its direct owner, then that owner's ancestors) minus every
 *  macro also in `otherChain` (the shared ancestors, which the cable stays inside). A macro port node never
 *  crosses its own macro's boundary (it already is the crossing), so its owner is left out. */
std::vector<juce::String> boundariesCrossed(const synth::MacroSet& macros, const juce::String& uuid,
                                            const std::vector<juce::String>& otherChain);

/** `uuid`'s direct owner then that owner's ancestors, innermost first; empty when `uuid` is in no macro. */
std::vector<juce::String> ownerChain(const synth::MacroSet& macros, const juce::String& uuid);

/** A macro with no direct members and no children: the only state MacroSet treats as dissolved. */
bool isEmptyMacro(const synth::MacroSet& macros, const juce::String& macroId);

} // namespace macro_nesting
