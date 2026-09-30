// MacroSelectionUnits.cpp
//
// Resolving a selection into grouping units (whole macros and loose nodes, each with its container).
// Declared in MacroSelectionUnits.h.

#include "MacroSelectionUnits.h"

#include "MacroGroupController.h"

#include <algorithm>
#include <set>

namespace macro_units {

namespace {

// A macro is WHOLE when every one of its non-port descendant members is selected (and it has at
// least one). Port nodes are boundary jacks drawn on the hull, not modules a marquee can pick up, so
// they never decide wholeness; selectMacro does select them, and they ride along with the unit.
bool isWhole(const synth::MacroSet& macros, const juce::String& macroId, const std::set<juce::String>& selected) {
    bool anyModule = false;
    for (const auto& uuid : macros.descendantMembers(macroId)) {
        const auto* owner = macros.findByMember(uuid);
        if (owner != nullptr && owner->memberIsPort(uuid))
            continue;
        if (selected.count(uuid) == 0)
            return false;
        anyModule = true;
    }
    return anyModule;
}

// The outermost whole macro in `ownerId`'s chain (the owner itself first, then outward), or empty.
juce::String outermostWhole(const synth::MacroSet& macros, const juce::String& ownerId,
                            const std::set<juce::String>& selected) {
    juce::String result = isWhole(macros, ownerId, selected) ? ownerId : juce::String();
    for (const auto& ancestorId : macros.ancestorChain(ownerId))
        if (isWhole(macros, ancestorId, selected))
            result = ancestorId; // the chain runs inner -> outer, so the last hit is the outermost
    return result;
}

} // namespace

// A selected node becomes part of the outermost macro above it whose every module is selected, else
// a loose unit in the macro that directly owns it. A selected port node whose macro is not whole is
// dropped rather than grouped: ports have their own add/delete affordances, and moving one out of its
// macro's members would break the "every port is a member" invariant.
std::vector<Unit> resolveUnits(const synth::MacroSet& macros, const std::vector<SelectedNode>& selection) {
    std::set<juce::String> selected;
    for (const auto& node : selection)
        if (node.uuid.isNotEmpty())
            selected.insert(node.uuid);

    std::vector<Unit> units;
    std::set<juce::String> seenMacros;
    for (const auto& node : selection) {
        const auto* owner = node.uuid.isNotEmpty() ? macros.findByMember(node.uuid) : nullptr;
        if (owner == nullptr) {
            units.push_back({{}, node.nodeId, node.uuid, {}});
            continue;
        }
        const juce::String wholeId = outermostWhole(macros, owner->id, selected);
        if (wholeId.isNotEmpty()) {
            if (seenMacros.insert(wholeId).second)
                units.push_back({wholeId, {}, {}, macros.parentOf(wholeId)});
            continue;
        }
        if (!owner->memberIsPort(node.uuid))
            units.push_back({{}, node.nodeId, node.uuid, owner->id});
    }
    return units;
}

std::optional<juce::String> commonContainer(const std::vector<Unit>& units) {
    if (units.empty())
        return std::nullopt;
    const juce::String container = units.front().container;
    for (const auto& unit : units)
        if (unit.container != container)
            return std::nullopt;
    return container;
}

bool allWholeMacros(const std::vector<Unit>& units) {
    return !units.empty() && std::all_of(units.begin(), units.end(), [](const Unit& u) { return u.isMacro(); });
}

std::vector<juce::String> insideUuids(const synth::MacroSet& macros, const std::vector<Unit>& units) {
    std::vector<juce::String> out;
    for (const auto& unit : units) {
        if (!unit.isMacro()) {
            if (unit.uuid.isNotEmpty())
                out.push_back(unit.uuid);
            continue;
        }
        for (const auto& uuid : macros.descendantMembers(unit.macroId))
            out.push_back(uuid);
    }
    return out;
}

std::vector<Unit> unitsFor(const MacroGroupController& controller, const synth::MacroSet& macros,
                           const std::vector<NodeID>& ids) {
    std::vector<SelectedNode> selection;
    selection.reserve(ids.size());
    for (auto id : ids)
        selection.push_back({id, controller.nodeUuidFor(id)});
    return resolveUnits(macros, selection);
}

std::vector<NodeID> insideNodeIdsFor(const MacroGroupController& controller, const synth::MacroSet& macros,
                                     const std::vector<Unit>& units) {
    std::vector<NodeID> out;
    for (const auto& unit : units) {
        if (!unit.isMacro()) {
            out.push_back(unit.nodeId);
            continue;
        }
        for (const auto& uuid : macros.descendantMembers(unit.macroId))
            if (const auto id = controller.resolveMemberNodeId(uuid); id.uid != 0)
                out.push_back(id);
    }
    return out;
}

} // namespace macro_units
