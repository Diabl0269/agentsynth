// MacroGroupControllerPortDrop.cpp
//
// A cable dropped on a collapsed macro's card: where it would land (cardCableDropAt, which the drag's hint and the drop
// both act on), the port it makes (or finds again), the chain of ports across every border it crosses, and the join of
// an empty port to the module it was made for. The rest of the port code is in MacroGroupControllerPorts.cpp.

#include "MacroGroupController.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AI/AIStateMapper/GraphRebuildBatch.h"
#include "AppUndoManager.h"
#include "MacroGroupControllerInternal.h"
#include "MacroNesting.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"
#include "UI/Graph/GraphEditor/GraphEditorTypes.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"

#include <algorithm>
#include <set>

namespace {
// The macros holding the target macro that the other end is not inside: borders a cable to the target's new port
// crosses on the target's side, innermost first.
std::vector<juce::String> targetBorders(const synth::MacroSet& macros, const juce::String& macroId,
                                        const juce::String& otherUuid) {
    const auto otherChain = macro_nesting::ownerChain(macros, otherUuid);
    std::vector<juce::String> borders;
    for (const auto& id : macros.ancestorChain(macroId))
        if (std::find(otherChain.begin(), otherChain.end(), id) == otherChain.end())
            borders.push_back(id);
    return borders;
}

std::vector<juce::String> outerFirst(std::vector<juce::String> innerFirst) {
    std::reverse(innerFirst.begin(), innerFirst.end());
    return innerFirst;
}
} // namespace

// A port of this macro (through the same chain of ports on the other end's own macros) that carries this very jack
// takes a repeated drop, so no second port or cable appears. The drag's hint asks the same question
// (cardCableDropAt), so it rings the port the drop leaves in place.
juce::AudioProcessorGraph::NodeID
MacroGroupController::portAlreadyCarryingDrop(const juce::String& macroId, bool newPortIsInput, bool isMidi,
                                              juce::AudioProcessorGraph::NodeID otherNodeId,
                                              int otherVisibleJack) const {
    auto reusedOther = otherNodeId;
    int reusedJack = otherVisibleJack;
    const auto& macrosNow = host_.getMacros();
    auto shared = macrosNow.ancestorChain(macroId);
    shared.insert(shared.begin(), macroId);
    for (const auto& crossedId : macro_nesting::boundariesCrossed(macrosNow, nodeUuidFor(otherNodeId), shared)) {
        const auto link = existingPortLinkedTo(crossedId, !newPortIsInput, reusedOther, reusedJack, isMidi);
        if (link.uid == 0)
            return {};
        reusedOther = link;
        reusedJack = 0;
    }
    for (const auto& crossedId : outerFirst(targetBorders(macrosNow, macroId, nodeUuidFor(otherNodeId)))) {
        const auto link =
            existingPortLinkedTo(crossedId, newPortIsInput, reusedOther, reusedJack, isMidi, /*otherOutside=*/true);
        if (link.uid == 0)
            return {};
        reusedOther = link;
        reusedJack = 0;
    }
    return existingPortLinkedTo(macroId, newPortIsInput, reusedOther, reusedJack, isMidi, /*otherOutside=*/true);
}

void MacroGroupController::createMacroPortFromDroppedCable(const juce::String& macroId, bool newPortIsInput,
                                                           bool isMidi, juce::AudioProcessorGraph::NodeID otherNodeId,
                                                           int otherVisibleJack) {
    auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr)
        return;

    // The same cable dropped again is already there: no second port or cable appears.
    if (portAlreadyCarryingDrop(macroId, newPortIsInput, isMidi, otherNodeId, otherVisibleJack).uid != 0)
        return;

    const auto kind = isMidi ? synth::MacroPortKind::Midi : synth::MacroPortKind::AudioCV;
    const juce::String typeName = macroPortNodeTypeName(newPortIsInput, kind);
    auto newProcessor = synth::AIStateMapper::createModule(typeName);
    if (!newProcessor)
        return;
    if (!isMidi) {
        // Infer the shape from the dragged cable's own jack fan (same getJackTargets read
        // resolvePolyLink already does) instead of always Mono. newPortIsInput true means the drag
        // started at an OUTPUT (this new port receives it), so the OTHER module's relevant side is
        // its output; false is the mirror.
        auto& graphForInference = host_.graph();
        auto* otherNodeForInference = graphForInference.getNodeForId(otherNodeId);
        auto* otherMbForInference = otherNodeForInference != nullptr
                                        ? dynamic_cast<ModuleBase*>(otherNodeForInference->getProcessor())
                                        : nullptr;
        const auto [inferredShape, inferredVoices] =
            inferPortShapeFromCableFan(otherMbForInference, otherVisibleJack, /*otherAsInput=*/!newPortIsInput);
        if (auto* inlet = dynamic_cast<MacroInletModule*>(newProcessor.get()))
            inlet->setPortShape(inferredShape, inferredVoices);
        else if (auto* outlet = dynamic_cast<MacroOutletModule*>(newProcessor.get()))
            outlet->setPortShape(inferredShape, inferredVoices);
    }

    // See addMacroPort's identical comment: dockMacroPortWidgets() (run from doCreate()'s own
    // updateComponents() below) places the widget for real.
    const auto placed = macro->bounds.getTopLeft();

    // A cable from one of several jacks (a Channel Strip's Left and Right) names the port after that jack, so two
    // drops make two distinguishable ports instead of two "Input"s.
    juce::String name = defaultMacroPortName(newPortIsInput, kind);
    if (!isMidi)
        if (auto* otherNode = host_.graph().getNodeForId(otherNodeId))
            if (auto* otherMb = dynamic_cast<ModuleBase*>(otherNode->getProcessor())) {
                const bool otherOutput = newPortIsInput;
                const int jacks =
                    otherOutput ? otherMb->getVisibleOutputPortCount() : otherMb->getVisibleInputPortCount();
                const auto label = otherOutput ? otherMb->getOutputPortLabel(otherVisibleJack)
                                               : otherMb->getInputPortLabel(otherVisibleJack);
                if (jacks > 1 && label.isNotEmpty())
                    name = label;
            }
    const int order = nextMacroPortOrder(*macro, newPortIsInput);

    // The new port's outward side lives in this macro's parent, so a cable to a node nested in some OTHER macro
    // crosses that node's own boundaries first: one port per boundary, innermost first, chained to the new port.
    // Every macro in this macro's own chain is shared ground, not a crossing.
    const auto& macros = host_.getMacros();
    auto sharedChain = macros.ancestorChain(macroId);
    sharedChain.insert(sharedChain.begin(), macroId);
    const auto otherCrossings = macro_nesting::boundariesCrossed(macros, nodeUuidFor(otherNodeId), sharedChain);

    // A target macro nested in a macro the other end sits outside of is crossed too: that macro gets a port as well,
    // outermost first, chained after the other end's own.
    const auto targetCrossings = outerFirst(targetBorders(macros, macroId, nodeUuidFor(otherNodeId)));

    auto& graph = host_.graph();
    auto proc = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(newProcessor));
    auto doCreate = [this, macroId, proc, placed, newPortIsInput, kind, name, order, isMidi, otherNodeId,
                     otherVisibleJack, otherCrossings, targetCrossings] {
        auto& g = host_.graph();
        const synth::GraphRebuildScope rebuildScope{g};
        if (!*proc)
            return;
        auto node = g.addNode(std::move(*proc), {}, juce::AudioProcessorGraph::UpdateKind::none);
        if (!node)
            return;
        node->properties.set("x", placed.x);
        node->properties.set("y", placed.y);
        const juce::String uuid = synth::AIStateMapper::ensureNodeUuid(node);

        auto* m = host_.getMacros().find(macroId);
        if (m == nullptr)
            return;
        m->members.push_back(uuid);
        synth::MacroPort port;
        port.nodeUuid = uuid;
        port.isInput = newPortIsInput;
        port.name = name;
        port.order = order;
        port.kind = kind;
        m->ports.push_back(port);

        // Wire the boundary through the SAME connectPorts a completed manual cable-drag uses.
        // recordUndo=false: already inside this method's own recordGraphAndMacroChange
        // transaction. Deliberately does NOT also wire anything on the INTERIOR side.
        if (g.getNodeForId(otherNodeId) != nullptr) {
            auto effectiveOther = otherNodeId;
            int effectiveJack = otherVisibleJack;
            for (const auto& crossedId : otherCrossings) {
                // The other end exits its macro through an outlet when it feeds the new port, enters through an
                // inlet when the new port feeds it.
                // A port this jack already leaves (or enters) the crossed macro through is reused, never doubled.
                const auto reused =
                    existingPortLinkedTo(crossedId, !newPortIsInput, effectiveOther, effectiveJack, isMidi);
                const auto chainPort = reused.uid != 0 ? reused
                                                       : mintMacroPortForAutoCreate(crossedId, !newPortIsInput, isMidi,
                                                                                    otherNodeId, otherVisibleJack);
                if (chainPort.uid == 0)
                    continue;
                if (reused.uid == 0) {
                    if (newPortIsInput)
                        host_.connectPorts(effectiveOther, effectiveJack, chainPort, 0, isMidi, /*recordUndo=*/false);
                    else
                        host_.connectPorts(chainPort, 0, effectiveOther, effectiveJack, isMidi, /*recordUndo=*/false);
                }
                effectiveOther = chainPort;
                effectiveJack = 0;
            }
            for (const auto& crossedId : targetCrossings) {
                const auto reused = existingPortLinkedTo(crossedId, newPortIsInput, effectiveOther, effectiveJack,
                                                         isMidi, /*otherOutside=*/true);
                const auto chainPort =
                    reused.uid != 0 ? reused
                                    : mintMacroPortForAutoCreate(crossedId, newPortIsInput, isMidi, node->nodeID, 0);
                if (chainPort.uid == 0)
                    continue;
                if (reused.uid == 0) {
                    if (newPortIsInput)
                        host_.connectPorts(effectiveOther, effectiveJack, chainPort, 0, isMidi, /*recordUndo=*/false);
                    else
                        host_.connectPorts(chainPort, 0, effectiveOther, effectiveJack, isMidi, /*recordUndo=*/false);
                }
                effectiveOther = chainPort;
                effectiveJack = 0;
            }
            if (newPortIsInput)
                host_.connectPorts(effectiveOther, effectiveJack, node->nodeID, 0, isMidi, /*recordUndo=*/false);
            else
                host_.connectPorts(node->nodeID, 0, effectiveOther, effectiveJack, isMidi, /*recordUndo=*/false);
        }

        host_.updateComponents();
        makeRoomFor("m:" + macroId);
        for (const auto& crossedId : otherCrossings)
            makeRoomFor("m:" + crossedId);
        for (const auto& crossedId : targetCrossings)
            makeRoomFor("m:" + crossedId);
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doCreate);
    else
        doCreate();

    host_.requestRepaint();
}

bool MacroGroupController::connectEmptyPortInterior(const juce::String& macroId,
                                                    juce::AudioProcessorGraph::NodeID portNodeId) {
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr)
        return false;
    const auto portUuid = nodeUuidFor(portNodeId);
    const synth::MacroPort* port = nullptr;
    for (const auto& p : macro->ports)
        if (p.nodeUuid == portUuid)
            port = &p;
    if (port == nullptr)
        return false;

    auto& g = host_.graph();
    const bool isMidi = port->kind == synth::MacroPortKind::Midi;
    for (const auto& c : g.getConnections())
        if (port->isInput ? c.source.nodeID == portNodeId : c.destination.nodeID == portNodeId)
            return false; // something already carries the signal on inside

    std::set<uint32_t> memberUids;
    for (const auto& uuid : macro->members)
        if (!macro->memberIsPort(uuid))
            memberUids.insert(resolveMemberNodeId(uuid).uid);

    // The module the port was made for: the first ordinary member with a jack of this kind that no other member
    // already feeds (an input) or takes from (an output), so the head of the chain gets an input, the tail an output.
    ModuleBase* target = nullptr;
    juce::AudioProcessorGraph::NodeID targetId;
    for (const auto& uuid : macro->members) {
        if (macro->memberIsPort(uuid))
            continue;
        const auto id = resolveMemberNodeId(uuid);
        auto* node = g.getNodeForId(id);
        auto* mb = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        if (mb == nullptr)
            continue;
        const bool hasJack =
            isMidi ? (port->isInput ? mb->acceptsMidi() : mb->producesMidi())
                   : (port->isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount()) > 0;
        if (!hasJack)
            continue;
        bool linked = false;
        for (const auto& c : g.getConnections()) {
            if (c.source.isMIDI() != isMidi)
                continue;
            if (port->isInput ? (c.destination.nodeID == id && memberUids.count(c.source.nodeID.uid) != 0)
                              : (c.source.nodeID == id && memberUids.count(c.destination.nodeID.uid) != 0))
                linked = true;
        }
        if (!linked) {
            target = mb;
            targetId = id;
            break;
        }
    }
    if (target == nullptr)
        return false;

    const synth::GraphRebuildScope rebuildScope{g};
    const auto shape = isMidi ? std::nullopt : stereoPortShape(portUuid);
    if (shape != MacroPortShape::Stereo) {
        if (port->isInput)
            host_.connectPorts(portNodeId, 0, targetId, 0, isMidi, /*recordUndo=*/false);
        else
            host_.connectPorts(targetId, 0, portNodeId, 0, isMidi, /*recordUndo=*/false);
        return true;
    }

    // A two-jack Stereo port: Left to the module's Left leg, Right to its Right leg (or onto the same jack when the
    // module has only one).
    const auto leftTargets = target->getJackTargets(0, port->isInput);
    if (leftTargets.empty())
        return false;
    const int leftRaw = leftTargets.front().rawHeadChannel;
    const int rightLeg = macro_ports::reachablePeerRightAudioLeg(target, port->isInput);
    const int rightRaw = rightLeg >= 0 ? rightLeg : leftRaw;
    const int portRight = MacroInletModule::kRightBase;
    static_assert(MacroOutletModule::kRightBase == MacroInletModule::kRightBase);
    for (const auto& [portCh, memberCh] : {std::pair{0, leftRaw}, std::pair{portRight, rightRaw}}) {
        if (port->isInput)
            g.addConnection({{portNodeId, portCh}, {targetId, memberCh}}, juce::AudioProcessorGraph::UpdateKind::none);
        else
            g.addConnection({{targetId, memberCh}, {portNodeId, portCh}}, juce::AudioProcessorGraph::UpdateKind::none);
    }
    return true;
}

// The first visible card containing the point wins, in the card array's order (a card is visible exactly while its
// macro is folded). An existing port's jack under the cursor takes the cable when its direction and kind match the
// drag and is refused otherwise: a mismatched jack never falls back to minting a port
// (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed). Anywhere else on the card the cable mints a
// port, unless a port of this macro already carries this very jack.
std::optional<MacroGroupController::CardCableDrop>
MacroGroupController::cardCableDropAt(juce::Point<int> screenPos, juce::AudioProcessorGraph::NodeID otherNode,
                                      int otherJack, bool dragFromInput, bool isMidi) {
    for (auto* card : host_.macroCards()) {
        if (card == nullptr || !card->isVisible())
            continue;
        const auto cardLocal = card->getLocalPoint(nullptr, screenPos);
        if (!card->getLocalBounds().contains(cardLocal))
            continue;

        CardCableDrop drop;
        drop.card = card;
        drop.macroId = card->getMacroId();
        // A drag from an OUTPUT looks for a destination, so the macro offers an input; from an INPUT, an output.
        drop.portIsInput = !dragFromInput;
        if (host_.graph().getNodeForId(otherNode) == nullptr)
            return drop; // refused: the cable's own end is gone

        if (const auto hit = macroCardPortForPoint(drop.macroId, cardLocal)) {
            const bool matches =
                hit->isInput == drop.portIsInput && (hit->kind == synth::MacroPortKind::Midi) == isMidi;
            const auto portNode = resolveMemberNodeId(hit->nodeUuid);
            if (matches && host_.graph().getNodeForId(portNode) != nullptr) {
                drop.kind = CardCableDrop::Kind::existingPort;
                drop.portUuid = hit->nodeUuid;
                drop.visibleJack = hit->visibleJack;
                drop.portNodeId = portNode;
            }
            return drop;
        }

        if (const auto carrying = portAlreadyCarryingDrop(drop.macroId, drop.portIsInput, isMidi, otherNode, otherJack);
            carrying.uid != 0) {
            drop.kind = CardCableDrop::Kind::alreadyLinked;
            drop.portUuid = nodeUuidFor(carrying);
            drop.portNodeId = carrying;
            return drop;
        }
        drop.kind = CardCableDrop::Kind::newPort;
        return drop;
    }
    return std::nullopt;
}
