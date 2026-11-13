// MacroSelectionUnits.h
//
// How a canvas selection resolves into grouping UNITS for Create Macro, Cmd+G and Add Selection to
// Macro (docs/macros/menu-and-membership.md#grouping-rules): whole macros plus loose nodes, each
// tagged with the macro that directly contains it. resolveUnits/commonContainer/insideUuids are pure
// MacroSet queries; the two *For* helpers read the live selection through MacroGroupController.
#pragma once

#include "MacroSet.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include <vector>

class MacroGroupController;

namespace macro_units {

using NodeID = juce::AudioProcessorGraph::NodeID;

/** One selected node: its graph id and persistent uuid (empty for a node never given one). */
struct SelectedNode {
    NodeID nodeId;
    juce::String uuid;
};

/** A whole macro (`macroId` set) or a loose node (`nodeId`/`uuid` set), plus the id of the macro that
 *  directly contains it (empty = top level). */
struct Unit {
    juce::String macroId;
    NodeID nodeId;
    juce::String uuid;
    juce::String container;

    bool isMacro() const { return macroId.isNotEmpty(); }
};

/** Units for `selection`, in selection order, each whole macro listed once. */
std::vector<Unit> resolveUnits(const synth::MacroSet& macros, const std::vector<SelectedNode>& selection);

/** The container every unit shares; nullopt when `units` is empty or spans more than one. */
std::optional<juce::String> commonContainer(const std::vector<Unit>& units);

/** True when `units` is non-empty and every unit is a whole macro. */
bool allWholeMacros(const std::vector<Unit>& units);

/** Every uuid a macro around `units` holds: the loose uuids plus each macro unit's descendantMembers. */
std::vector<juce::String> insideUuids(const synth::MacroSet& macros, const std::vector<Unit>& units);

/** resolveUnits over `ids`, uuids read (never assigned) through the controller. */
std::vector<Unit> unitsFor(const MacroGroupController& controller, const synth::MacroSet& macros,
                           const std::vector<NodeID>& ids);

/** insideUuids as live node ids, loose nodes without a uuid included. */
std::vector<NodeID> insideNodeIdsFor(const MacroGroupController& controller, const synth::MacroSet& macros,
                                     const std::vector<Unit>& units);

} // namespace macro_units
