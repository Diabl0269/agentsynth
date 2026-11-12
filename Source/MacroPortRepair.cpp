// Concern: repairing port modules no macro lists (MacroPortRepair.h).
#include "MacroPortRepair.h"

#include "Modules/MacroInletModule.h"
#include "Modules/MacroMidiInletModule.h"
#include "Modules/MacroMidiOutletModule.h"
#include "Modules/MacroOutletModule.h"
#include <map>
#include <set>

namespace synth {

namespace {

struct PortFacts {
    bool isInput = false;
    MacroPortKind kind = MacroPortKind::AudioCV;
};

PortFacts factsOf(const juce::AudioProcessor* p) {
    PortFacts facts;
    facts.isInput =
        dynamic_cast<const MacroInletModule*>(p) != nullptr || dynamic_cast<const MacroMidiInletModule*>(p) != nullptr;
    const bool midi = dynamic_cast<const MacroMidiInletModule*>(p) != nullptr ||
                      dynamic_cast<const MacroMidiOutletModule*>(p) != nullptr;
    facts.kind = midi ? MacroPortKind::Midi : MacroPortKind::AudioCV;
    return facts;
}

juce::String defaultName(const PortFacts& facts) {
    if (facts.kind == MacroPortKind::Midi)
        return facts.isInput ? "MIDI In" : "MIDI Out";
    return facts.isInput ? "Input" : "Output";
}

int nextOrder(const Macro& macro, bool isInput) {
    int next = 0;
    for (const auto& port : macro.ports)
        if (port.isInput == isInput)
            next = std::max(next, port.order + 1);
    return next;
}

} // namespace

bool isMacroPortModule(const juce::AudioProcessor* processor) {
    return dynamic_cast<const MacroInletModule*>(processor) != nullptr ||
           dynamic_cast<const MacroOutletModule*>(processor) != nullptr ||
           dynamic_cast<const MacroMidiInletModule*>(processor) != nullptr ||
           dynamic_cast<const MacroMidiOutletModule*>(processor) != nullptr;
}

bool isStrayMacroPort(const MacroSet& macros, const juce::String& uuid, const juce::AudioProcessor* processor) {
    if (uuid.isEmpty() || !isMacroPortModule(processor))
        return false;
    const auto* owner = macros.findByMember(uuid);
    return owner == nullptr || !owner->memberIsPort(uuid);
}

// The macros of a stray's neighbours (the nodes its cables join) are the macros it could front: adopted only when
// that is exactly one, since two would be a guess.
int adoptStrayMacroPorts(const juce::AudioProcessorGraph& graph, MacroSet& macros) {
    std::map<juce::AudioProcessorGraph::NodeID, juce::String> uuidOf;
    for (auto* node : graph.getNodes())
        uuidOf[node->nodeID] = node->properties["uuid"].toString();

    int adopted = 0;
    for (auto* node : graph.getNodes()) {
        const auto& uuid = uuidOf[node->nodeID];
        if (!isStrayMacroPort(macros, uuid, node->getProcessor()))
            continue;

        std::set<juce::String> owners;
        if (const auto* own = macros.findByMember(uuid))
            owners.insert(own->id); // already a member (not a port): it fronts that macro
        else
            for (const auto& c : graph.getConnections()) {
                const auto other = c.source.nodeID == node->nodeID        ? c.destination.nodeID
                                   : c.destination.nodeID == node->nodeID ? c.source.nodeID
                                                                          : juce::AudioProcessorGraph::NodeID{};
                if (other.uid == 0)
                    continue;
                const auto otherUuid = uuidOf.find(other);
                if (otherUuid == uuidOf.end() || otherUuid->second == uuid)
                    continue;
                if (const auto* owner = macros.findByMember(otherUuid->second))
                    owners.insert(owner->id);
            }
        if (owners.size() != 1)
            continue;

        auto* macro = macros.find(*owners.begin());
        if (macro == nullptr)
            continue;
        const auto facts = factsOf(node->getProcessor());
        if (!macro->hasMember(uuid))
            macro->members.push_back(uuid);
        MacroPort port;
        port.nodeUuid = uuid;
        port.isInput = facts.isInput;
        port.kind = facts.kind;
        port.name = defaultName(facts);
        port.order = nextOrder(*macro, facts.isInput);
        macro->ports.push_back(std::move(port));
        ++adopted;
    }
    return adopted;
}

} // namespace synth
