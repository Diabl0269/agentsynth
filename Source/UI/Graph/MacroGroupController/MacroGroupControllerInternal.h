#pragma once

// MacroGroupControllerInternal.h -- private helpers shared between MacroGroupController's units, for a pass that asks
// about many hulls at once (docs/architecture/graph-queries.md). Not part of MacroGroupController.h.

#include "MacroGroupController.h"
#include <unordered_map>

namespace macro_geometry {

using CardsByNodeUid = std::unordered_map<uint32_t, ModuleComponent*>;

/** Every module card by node uid, built once for a pass over many hulls. */
CardsByNodeUid cardsByNodeUid(GraphCanvasHost& host);

/** MacroGroupController::macroHullBounds(macroId), measured against the pass's card map. */
juce::Rectangle<int> openHullBounds(GraphCanvasHost& host, const CardsByNodeUid& cards, const juce::String& macroId);

} // namespace macro_geometry

namespace macro_toggle {

/** The macro a selected node stands for in a collapse toggle: the outermost collapsed macro above it (a hidden node's
 *  card is what the user sees), else its direct owner. */
inline const synth::Macro* targetFor(const synth::MacroSet& macros, const juce::String& uuid) {
    const juce::String collapsedId = macros.outermostCollapsedAncestorOf(uuid);
    return collapsedId.isNotEmpty() ? macros.find(collapsedId) : macros.findByMember(uuid);
}

} // namespace macro_toggle
