// MacroGroupControllerBypassMute.cpp
//
// Deleting a macro and its members, and the macro bypass/mute fan-out (P8-15d, T142,
// docs/macros/ports.md#bypass-and-mute). MacroGroupController is declared in MacroGroupController.h;
// sibling MacroGroupController*.cpp files in this directory hold the rest of the class.

#include "MacroGroupController.h"

#include "AppUndoManager.h"
#include "Modules/ModuleBase.h"

void MacroGroupController::deleteMacroAndMembers(const juce::String& macroId) {
    auto* m = host_.getMacros().find(macroId);
    if (m == nullptr)
        return;

    std::vector<juce::AudioProcessorGraph::NodeID> memberIds;
    for (const auto& uuid : m->members) {
        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid != 0)
            memberIds.push_back(nodeId);
    }

    // Reuses the single delete-selection path exactly, rather than a parallel "delete a macro"
    // mutation: selecting every member and calling deleteSelection() gets the same undo/dirty/
    // timeline-reconcile handling deleting any other multi-selection gets, and
    // updateComponents() dissolves the now-empty macro as part of that same step.
    host_.setSelectedNodes(memberIds);
    host_.deleteSelection();
}

std::vector<juce::AudioProcessorGraph::NodeID>
MacroGroupController::resolvedMacroMemberModuleNodes(const juce::String& macroId) const {
    std::vector<juce::AudioProcessorGraph::NodeID> result;
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr)
        return result;

    auto& graph = host_.graph();
    for (const auto& uuid : macro->members) {
        auto nodeId = resolveMemberNodeId(uuid);
        if (nodeId.uid == 0)
            continue;
        auto* node = graph.getNodeForId(nodeId);
        if (node != nullptr && dynamic_cast<ModuleBase*>(node->getProcessor()) != nullptr)
            result.push_back(nodeId);
    }
    return result;
}

bool MacroGroupController::macroHasMuteEligibleMember(const juce::String& macroId) const {
    auto& graph = host_.graph();
    for (auto nodeId : resolvedMacroMemberModuleNodes(macroId)) {
        auto* node = graph.getNodeForId(nodeId);
        auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        if (mb != nullptr && mb->hasMuteParameter())
            return true;
    }
    return false;
}

namespace {
// The members a macro's Bypass fan-out touches. For an ordinary macro that is every member. For a
// CHANNEL macro — one containing a Channel Strip — the source node(s) and the strip itself are
// skipped (docs/mixer/mixer.md#bypass-and-mute): "bypass" on a channel means "bypass the inserts", so the chain's
// effects go dry while the source keeps producing and the strip keeps passing signal. Mute has no
// such carve-out — muting a channel macro mutes the strip too.
std::vector<juce::AudioProcessorGraph::NodeID>
bypassFanOutMembers(juce::AudioProcessorGraph& graph, std::vector<juce::AudioProcessorGraph::NodeID> members) {
    auto typeOf = [&graph](juce::AudioProcessorGraph::NodeID nodeId) -> std::optional<ModuleType> {
        auto* node = graph.getNodeForId(nodeId);
        auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        return mb != nullptr ? std::optional<ModuleType>(mb->getModuleType()) : std::nullopt;
    };
    const bool isChannelMacro = std::any_of(members.begin(), members.end(),
                                            [&](auto nodeId) { return typeOf(nodeId) == ModuleType::ChannelStrip; });
    if (!isChannelMacro)
        return members;
    members.erase(std::remove_if(members.begin(), members.end(),
                                 [&](auto nodeId) {
                                     const auto type = typeOf(nodeId);
                                     return type == ModuleType::ChannelStrip ||
                                            type == ModuleType::TimelineMidiSource ||
                                            type == ModuleType::TimelineAudioSource;
                                 }),
                  members.end());
    return members;
}
} // namespace

MacroGroupController::MacroToggleState MacroGroupController::macroBypassState(const juce::String& macroId) const {
    auto& graph = host_.graph();
    bool anyOn = false;
    bool anyOff = false;
    // Reports over exactly the members the fan-out below toggles, so a channel macro whose inserts
    // are all bypassed reads AllOn even though its strip and source never are.
    for (auto nodeId : bypassFanOutMembers(graph, resolvedMacroMemberModuleNodes(macroId))) {
        auto* node = graph.getNodeForId(nodeId);
        auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        if (mb == nullptr)
            continue;
        (mb->isBypassed() ? anyOn : anyOff) = true;
    }
    if (anyOn && anyOff)
        return MacroToggleState::Mixed;
    return anyOn ? MacroToggleState::AllOn : MacroToggleState::AllOff;
}

MacroGroupController::MacroToggleState MacroGroupController::macroMuteState(const juce::String& macroId) const {
    auto& graph = host_.graph();
    bool anyOn = false;
    bool anyOff = false;
    for (auto nodeId : resolvedMacroMemberModuleNodes(macroId)) {
        auto* node = graph.getNodeForId(nodeId);
        auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        // Skip members with no "muted" parameter (Macro In/Out and their MIDI variants among
        // them, §7 item 1's note) — they have nothing to report either way.
        if (mb == nullptr || !mb->hasMuteParameter())
            continue;
        (mb->isMuted() ? anyOn : anyOff) = true;
    }
    if (anyOn && anyOff)
        return MacroToggleState::Mixed;
    return anyOn ? MacroToggleState::AllOn : MacroToggleState::AllOff;
}

void MacroGroupController::setMacroBypassed(const juce::String& macroId, bool bypassed) {
    // A channel macro skips its source and strip — see bypassFanOutMembers.
    const auto memberNodes = bypassFanOutMembers(host_.graph(), resolvedMacroMemberModuleNodes(macroId));
    if (memberNodes.empty())
        return;

    // The fan-out is an ORDINARY parameter change (ModuleBase::setBypassed is already
    // setValueNotifyingHost under the hood) batched into ONE undo step via the same before/after
    // graph-JSON snapshot applySmartSuggestions uses. Each member's own processBlock keeps
    // honouring the two-branch bypass/mute contract exactly as it does for a per-module toggle.
    auto& graph = host_.graph();
    auto doBypass = [this, memberNodes, bypassed] {
        auto& g = host_.graph();
        for (auto nodeId : memberNodes) {
            auto* node = g.getNodeForId(nodeId);
            if (auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr)
                mb->setBypassed(bypassed);
        }
    };

    if (host_.undo())
        host_.undo()->recordStructuralChange(graph, doBypass);
    else
        doBypass();

    // A collapsed macro's own card is not one of the members whose parameterValueChanged listener
    // would otherwise schedule this repaint -- it reads macroBypassState() fresh on every paint,
    // so it has to be told a repaint is due.
    host_.repaintCanvas();
}

void MacroGroupController::setMacroMuted(const juce::String& macroId, bool muted) {
    if (!macroHasMuteEligibleMember(macroId))
        return;

    const auto memberNodes = resolvedMacroMemberModuleNodes(macroId);
    auto& graph = host_.graph();
    auto doMute = [this, memberNodes, muted] {
        auto& g = host_.graph();
        for (auto nodeId : memberNodes) {
            auto* node = g.getNodeForId(nodeId);
            auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
            // A member with no "muted" parameter is left alone rather than calling setMuted,
            // which dereferences an unset mutedParam unconditionally.
            if (mb != nullptr && mb->hasMuteParameter())
                mb->setMuted(muted);
        }
    };

    if (host_.undo())
        host_.undo()->recordStructuralChange(graph, doMute);
    else
        doMute();

    // See setMacroBypassed's matching comment.
    host_.repaintCanvas();
}

void MacroGroupController::toggleMacroBypassed(const juce::String& macroId) {
    if (host_.getMacros().find(macroId) == nullptr) {
        host_.reportStatusMessage("Select a macro to bypass or enable it.");
        return;
    }

    // Converge toward bypassing everything first (Mixed or AllOff -> bypass all; AllOn -> clear
    // all) — the same direction toggleSelectionMacrosCollapsed converges a mixed selection
    // toward collapsed, for the same reason.
    setMacroBypassed(macroId, macroBypassState(macroId) != MacroToggleState::AllOn);
}

void MacroGroupController::toggleMacroMuted(const juce::String& macroId) {
    if (!macroHasMuteEligibleMember(macroId)) {
        host_.reportStatusMessage("This macro has no member that can be muted.");
        return;
    }

    setMacroMuted(macroId, macroMuteState(macroId) != MacroToggleState::AllOn);
}
