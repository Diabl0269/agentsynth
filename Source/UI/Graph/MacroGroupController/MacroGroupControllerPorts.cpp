// MacroGroupControllerPorts.cpp
//
// Macro port CRUD: auto-delete of orphaned ports, add/remove/rename/reorder/re-shape a macro
// port, and creating a port from a dropped cable. MacroGroupController is declared in
// MacroGroupController.h; sibling MacroGroupController*.cpp files in this directory hold the rest
// of the class. The "Configure I/O" dialog itself (promptConfigureMacroIO/promptRenameMacroPort)
// stays on GraphEditor (GraphEditorMacroPrompts.cpp) — see MacroGroupController.h's class comment
// for why (juce::Component::SafePointer<GraphEditor> needs a genuine GraphEditor&).

#include "MacroGroupController.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "MacroNesting.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"

namespace {
// Mirrors GraphEditor::rightAudioLegOf + audioChannelReachableFromJack's rule (the Dual
// I/O toggle's own "which raw channel is this peer's right leg, and can the user actually reach
// it" lookup) without pulling in GraphEditor.h, which MacroGroupController deliberately never
// includes (MacroGroupController.h's class comment: it reaches its host only through
// GraphCanvasHost). A collapsed split-block peer's hidden kRightBase must not get a cable nobody
// can unplug, exactly as it mustn't for an ordinary Dual I/O toggle.
int reachablePeerRightAudioLeg(juce::AudioProcessor* proc, bool asInput) {
    auto* peerMb = dynamic_cast<ModuleBase*>(proc);
    if (peerMb == nullptr) {
        // Graph I/O (Audio Input/Output) is a plain contiguous pair, same as rightAudioLegOf.
        const int channels = asInput ? proc->getTotalNumInputChannels() : proc->getTotalNumOutputChannels();
        return channels >= 2 ? 1 : -1;
    }
    const int leg = peerMb->rightAudioLegChannel();
    if (leg < 0)
        return -1;
    const int channels = asInput ? proc->getTotalNumInputChannels() : proc->getTotalNumOutputChannels();
    if (leg >= channels)
        return -1;
    const LogicalPort port = asInput ? peerMb->mapInputChannel(leg) : peerMb->mapOutputChannel(leg);
    if (port.role != PortRole::Audio)
        return -1;

    const int visible = asInput ? peerMb->getVisibleInputPortCount() : peerMb->getVisibleOutputPortCount();
    for (int jack = 0; jack < visible; ++jack)
        for (const auto& t : peerMb->getJackTargets(jack, asInput))
            for (int v = 0; v < t.voiceSpan; ++v)
                if (t.rawHeadChannel + v == leg)
                    return leg;
    return -1; // reported but hidden behind a collapsed jack — not reachable
}
} // namespace

void MacroGroupController::autoDeleteOrphanedMacroPort(juce::AudioProcessorGraph::NodeID nodeId) {
    if (!host_.getAutoDeleteMacroPortsOnLastCableEnabled())
        return;

    const juce::String uuid = nodeUuidFor(nodeId);
    if (uuid.isEmpty())
        return;
    auto* m = host_.getMacros().findByMember(uuid);
    if (m == nullptr || !m->memberIsPort(uuid))
        return;

    auto& graph = host_.graph();
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == nodeId || c.destination.nodeID == nodeId)
            return; // still has at least one cable — survives

    const auto macroId = m->id;
    spliceOutMacroPort(*m, uuid);
    if (macro_nesting::isEmptyMacro(host_.getMacros(), macroId))
        host_.getMacros().remove(macroId); // MacroSet::removeMemberEverywhere's own "zero members" rule
}

// A bounded ONE-extra-hop special case. Almost
// every real macro-port-to-modulation-target crossing is spliced through a hidden
// AttenuverterModule (AudioEngine::addModRouting always wraps a CV routing as source->
// attenuverter(ch0)->destination), and an attenuverter can never itself be a macro member
// (docs/macros/ports.md) — so a deleted node's DIRECT neighbour being the attenuverter, not the
// macro port, meant the port two hops away was never revisited and could never auto-delete via
// its exterior leg going to zero. Called for every neighbour macroPortDeletionNeighbors()
// captured for a just-deleted node (deleteSelection/requestDeleteModule), the SAME candidate list
// autoDeleteOrphanedMacroPort() below already sweeps for the direct (non-attenuverter) case — a
// no-op on any neighbour that isn't an attenuverter, or whose OTHER leg isn't a macro port, or
// that still has more than one connection (a live mod chain still using both legs). No general
// multi-hop walk: this reaches exactly one hop past the attenuverter and stops.
void MacroGroupController::autoDeleteOrphanedAttenuverter(juce::AudioProcessorGraph::NodeID nodeId) {
    if (!host_.getAutoDeleteMacroPortsOnLastCableEnabled())
        return;

    auto& graph = host_.graph();
    auto* node = graph.getNodeForId(nodeId);
    if (node == nullptr || dynamic_cast<AttenuverterModule*>(node->getProcessor()) == nullptr)
        return;

    std::vector<juce::AudioProcessorGraph::Connection> remaining;
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == nodeId || c.destination.nodeID == nodeId)
            remaining.push_back(c);
    if (remaining.size() != 1)
        return; // still bridging two live connections — not orphaned

    const auto& only = remaining.front();
    const auto otherId = only.source.nodeID == nodeId ? only.destination.nodeID : only.source.nodeID;
    const juce::String portUuid = nodeUuidFor(otherId);
    if (portUuid.isEmpty())
        return;
    auto* m = host_.getMacros().findByMember(portUuid);
    if (m == nullptr || !m->memberIsPort(portUuid))
        return; // the attenuverter's one remaining leg goes somewhere real, not this bounded case

    host_.clearModMatrixRows();
    graph.removeNode(nodeId); // drops the attenuverter's own edge to the port too

    // "Treat the port as orphaned" only if nothing exterior is left on it. A macro-interior
    // source can fan out through more than one attenuverter (e.g. an Envelope feeding both a
    // Filter's cutoff and a VCA's gain through two separate attenuverters on the same port) —
    // deleting one destination must only remove ITS attenuverter, leaving the port wired for the
    // other fan-out leg. So after the attenuverter is gone, re-scan the port's remaining
    // connections: any edge whose other end is not an interior member of this macro (another
    // attenuverter headed elsewhere, or anything else outside) means the port still has work to
    // do, and only the attenuverter is removed. Only when every remaining edge is interior do we
    // splice the port out directly, sharing spliceOutMacroPort with every other
    // auto-delete/ungroup path rather than going through autoDeleteOrphanedMacroPort()'s own
    // "zero cables total" test above — that test is right for the direct (non-attenuverter) case,
    // but a lone interior leg would make it say this port "survives", which is exactly the
    // gap this one-extra-hop case closes.
    const auto portId = resolveMemberNodeId(portUuid);
    bool hasExteriorConnection = false;
    if (portId.uid != 0) {
        for (const auto& c : graph.getConnections()) {
            juce::AudioProcessorGraph::NodeID other;
            if (c.source.nodeID == portId)
                other = c.destination.nodeID;
            else if (c.destination.nodeID == portId)
                other = c.source.nodeID;
            else
                continue;
            const juce::String otherUuid = nodeUuidFor(other);
            const bool otherIsInteriorMember =
                otherUuid.isNotEmpty() &&
                std::find(m->members.begin(), m->members.end(), otherUuid) != m->members.end() &&
                !m->memberIsPort(otherUuid);
            if (!otherIsInteriorMember) {
                hasExteriorConnection = true;
                break;
            }
        }
    }
    if (hasExteriorConnection)
        return; // still feeding another fan-out leg (e.g. a second attenuverter) — keep the port

    const auto macroId = m->id;
    spliceOutMacroPort(*m, portUuid);
    if (macro_nesting::isEmptyMacro(host_.getMacros(), macroId))
        host_.getMacros().remove(macroId); // matches autoDeleteOrphanedMacroPort's own zero-members rule
}

std::vector<juce::AudioProcessorGraph::NodeID> MacroGroupController::macroPortDeletionNeighbors(
    const std::vector<juce::AudioProcessorGraph::NodeID>& deletedIds) const {
    const auto isBeingDeleted = [&](juce::AudioProcessorGraph::NodeID id) {
        return std::find(deletedIds.begin(), deletedIds.end(), id) != deletedIds.end();
    };
    std::vector<juce::AudioProcessorGraph::NodeID> neighbors;
    for (const auto& c : host_.graph().getConnections()) {
        if (isBeingDeleted(c.source.nodeID) && !isBeingDeleted(c.destination.nodeID))
            neighbors.push_back(c.destination.nodeID);
        else if (isBeingDeleted(c.destination.nodeID) && !isBeingDeleted(c.source.nodeID))
            neighbors.push_back(c.source.nodeID);
    }
    std::sort(neighbors.begin(), neighbors.end(), [](auto a, auto b) { return a.uid < b.uid; });
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    return neighbors;
}

juce::String MacroGroupController::addMacroPort(const juce::String& macroId, bool isInput, synth::MacroPortKind kind,
                                                MacroPortShape shape, int voiceCount, const juce::String& portName) {
    auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr)
        return {};

    const juce::String typeName = macroPortNodeTypeName(isInput, kind);
    auto newProcessor = synth::AIStateMapper::createModule(typeName);
    if (!newProcessor)
        return {};

    // Shape is set BEFORE the node is wired into the live graph, honouring
    // docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed's "decided at construction, then fixed"
    // rule — meaningless (and skipped) for a MIDI port (docs/macros/ports.md#node-types).
    if (kind == synth::MacroPortKind::AudioCV) {
        if (auto* inlet = dynamic_cast<MacroInletModule*>(newProcessor.get()))
            inlet->setPortShape(shape, voiceCount);
        else if (auto* outlet = dynamic_cast<MacroOutletModule*>(newProcessor.get()))
            outlet->setPortShape(shape, voiceCount);
    }

    // Placement: a port's widget is DOCKED to its macro's hull, derived fresh by
    // dockMacroPortWidgets() at the end of every updateComponents() pass — this just seeds a
    // harmless position that the SAME updateComponents() call immediately overrides.
    const auto placed = macro->bounds.getTopLeft();

    const juce::String name = portName.trim().isNotEmpty() ? portName.trim() : defaultMacroPortName(isInput, kind);
    const int order = nextMacroPortOrder(*macro, isInput);

    auto& graph = host_.graph();
    auto proc = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(newProcessor));
    juce::String newUuid;
    auto doAdd = [this, macroId, proc, placed, isInput, kind, name, order, &newUuid] {
        if (!*proc)
            return;
        auto node = host_.graph().addNode(std::move(*proc));
        if (!node)
            return;
        node->properties.set("x", placed.x);
        node->properties.set("y", placed.y);
        const juce::String uuid = synth::AIStateMapper::ensureNodeUuid(node);
        newUuid = uuid;

        auto* m = host_.getMacros().find(macroId);
        if (m == nullptr)
            return; // defensive: the macro shouldn't vanish mid-transaction

        m->members.push_back(uuid);
        synth::MacroPort port;
        port.nodeUuid = uuid;
        port.isInput = isInput;
        port.name = name;
        port.order = order;
        port.kind = kind;
        m->ports.push_back(port);

        host_.updateComponents();
        makeRoomFor("m:" + macroId);
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doAdd);
    else
        doAdd();

    host_.requestRepaint();
    return newUuid;
}

void MacroGroupController::removeMacroPort(const juce::String&, const juce::String& nodeUuid) {
    const auto nodeId = resolveMemberNodeId(nodeUuid);
    if (nodeId.uid == 0)
        return;

    // Reuses the ordinary multi-select delete path exactly, the same way deleteMacroAndMembers
    // reuses it for a whole macro.
    host_.setSelectedNodes({nodeId});
    host_.deleteSelection();
}

void MacroGroupController::deleteMacroPortNode(const juce::String& macroId, const juce::String& nodeUuid) {
    auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr || !macro->memberIsPort(nodeUuid))
        return;

    auto& graph = host_.graph();

    // Splices the boundary cable back (spliceOutMacroPort, the same helper ungroupSelection()
    // uses) rather than dropping it like removeMacroPort() above.
    auto doDelete = [this, macroId, nodeUuid] {
        auto* m = host_.getMacros().find(macroId);
        if (m == nullptr)
            return; // defensive: shouldn't happen mid-transaction
        spliceOutMacroPort(*m, nodeUuid);
        if (macro_nesting::isEmptyMacro(host_.getMacros(), macroId))
            host_.getMacros().remove(macroId); // matches MacroSet::removeMemberEverywhere's own zero-members rule
        host_.updateComponents();
        returnDisplacedNeighbours(macroId, /*keepBlocked=*/true); // no-op when the macro just dissolved
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doDelete);
    else
        doDelete();

    host_.requestRepaint();
}

// Default DROP for both manual delete paths — Configure
// I/O's Delete Port (removeMacroPort) and the port's own right-click Delete Port used to disagree
// (the latter always spliced via deleteMacroPortNode). Both UI call sites now go through here
// instead of calling either primitive directly, so a flip of the "splice the cable back"
// preference always applies to both at once. Ungroup and the auto-delete-on-last-cable path are
// untouched — neither goes through this.
void MacroGroupController::deleteMacroPortManually(const juce::String& macroId, const juce::String& nodeUuid) {
    if (host_.getSpliceCableOnMacroPortDeleteEnabled())
        deleteMacroPortNode(macroId, nodeUuid);
    else
        removeMacroPort(macroId, nodeUuid);
}

void MacroGroupController::deleteBottomMacroPort(const juce::String& macroId, bool isInput) {
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr)
        return;
    const synth::MacroPort* bottom = nullptr;
    for (const auto& p : macro->ports)
        if (p.isInput == isInput && (bottom == nullptr || p.order > bottom->order))
            bottom = &p;
    if (bottom == nullptr)
        return;
    const juce::String uuid = bottom->nodeUuid; // the delete rebuilds the set `bottom` points into
    deleteMacroPortManually(macroId, uuid);
}

juce::PopupMenu MacroGroupController::buildAddPortMenu(const juce::String& macroId, bool isInput) {
    // The SAME kind/shape choice list synth::ui::MacroPortConfigDialog's own "Add a port" panel offers
    // (Audio/CV picks Mono/Stereo/Poly-N, MIDI has no shape) and, on a choice, the SAME addMacroPort()
    // Configure I/O's Add button calls, so the created port and its one-undo-step transaction are identical
    // either way. `isInput` is fixed by which side's '+' was clicked; an empty name falls back to
    // addMacroPort's own defaultMacroPortName(). The Poly-N voice count matches the dialog's own default ("4").
    // The menu is async, so its actions hold a weak reference: the editor can go away while it is open.
    juce::PopupMenu menu;
    juce::WeakReference<MacroGroupController> weakThis(this);
    auto addItem = [&menu, weakThis, macroId, isInput](const juce::String& text, synth::MacroPortKind kind,
                                                       MacroPortShape shape, int voices) {
        menu.addItem(text, [weakThis, macroId, isInput, kind, shape, voices] {
            if (weakThis != nullptr)
                weakThis->addMacroPort(macroId, isInput, kind, shape, voices, {});
        });
    };
    addItem("Audio/CV - Mono", synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1);
    addItem("Audio/CV - Stereo", synth::MacroPortKind::AudioCV, MacroPortShape::Stereo, 1);
    addItem("Audio/CV - Poly-N", synth::MacroPortKind::AudioCV, MacroPortShape::Poly, 4);
    addItem("MIDI", synth::MacroPortKind::Midi, MacroPortShape::Mono, 1);
    return menu;
}

void MacroGroupController::renameMacroPort(const juce::String& macroId, const juce::String& nodeUuid,
                                           const juce::String& newName) {
    const auto trimmed = newName.trim();
    if (trimmed.isEmpty())
        return; // empty/whitespace-only input cancels without renaming

    auto& graph = host_.graph();
    auto doRename = [this, macroId, nodeUuid, trimmed] {
        if (auto* m = host_.getMacros().find(macroId))
            for (auto& p : m->ports)
                if (p.nodeUuid == nodeUuid) {
                    p.name = trimmed;
                    break;
                }
        host_.updateComponents(); // a new name can change the sidebar strip width, so re-dock
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doRename);
    else
        doRename();
}

void MacroGroupController::changeMacroPortColour(const juce::String& macroId, const juce::String& nodeUuid,
                                                 std::optional<juce::Colour> newColour) {
    auto& graph = host_.graph();
    auto doChange = [this, macroId, nodeUuid, newColour] {
        if (auto* m = host_.getMacros().find(macroId))
            for (auto& p : m->ports)
                if (p.nodeUuid == nodeUuid) {
                    p.colour = newColour;
                    break;
                }
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doChange);
    else
        doChange();

    // Mirror setMacroColour: a macro-set change is not itself a graph change, so nothing repaints
    // the canvas on its own.
    host_.requestRepaint();
}

void MacroGroupController::moveMacroPortOrder(const juce::String& macroId, const juce::String& nodeUuid, bool moveUp) {
    auto& graph = host_.graph();
    auto doMove = [this, macroId, nodeUuid, moveUp] {
        auto* m = host_.getMacros().find(macroId);
        if (m == nullptr)
            return;

        auto selfIt = std::find_if(m->ports.begin(), m->ports.end(),
                                   [&](const synth::MacroPort& p) { return p.nodeUuid == nodeUuid; });
        if (selfIt == m->ports.end())
            return;
        const bool isInput = selfIt->isInput;

        // Reordering is scoped to one side of the card (inputs against inputs, outputs against
        // outputs).
        std::vector<synth::MacroPort*> group;
        for (auto& p : m->ports)
            if (p.isInput == isInput)
                group.push_back(&p);
        std::sort(group.begin(), group.end(),
                  [](const synth::MacroPort* a, const synth::MacroPort* b) { return a->order < b->order; });

        int idx = -1;
        for (size_t i = 0; i < group.size(); ++i)
            if (group[i]->nodeUuid == nodeUuid) {
                idx = (int)i;
                break;
            }
        if (idx < 0)
            return;

        const int otherIdx = moveUp ? idx - 1 : idx + 1;
        if (otherIdx < 0 || otherIdx >= (int)group.size())
            return; // already at the edge of its group — no-op, no undo entry pushed

        std::swap(group[(size_t)idx]->order, group[(size_t)otherIdx]->order);
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doMove);
    else
        doMove();
}

void MacroGroupController::reorderMacroPortToIndex(const juce::String& macroId, const juce::String& nodeUuid,
                                                   int newIndexInGroup) {
    auto& graph = host_.graph();
    auto doMove = [this, macroId, nodeUuid, newIndexInGroup] {
        auto* m = host_.getMacros().find(macroId);
        if (m == nullptr)
            return;

        auto selfIt = std::find_if(m->ports.begin(), m->ports.end(),
                                   [&](const synth::MacroPort& p) { return p.nodeUuid == nodeUuid; });
        if (selfIt == m->ports.end())
            return;
        const bool isInput = selfIt->isInput;

        std::vector<synth::MacroPort*> group;
        for (auto& p : m->ports)
            if (p.isInput == isInput)
                group.push_back(&p);
        std::sort(group.begin(), group.end(),
                  [](const synth::MacroPort* a, const synth::MacroPort* b) { return a->order < b->order; });

        int idx = -1;
        for (size_t i = 0; i < group.size(); ++i)
            if (group[i]->nodeUuid == nodeUuid) {
                idx = (int)i;
                break;
            }
        if (idx < 0)
            return;

        const int clampedTarget = juce::jlimit(0, (int)group.size() - 1, newIndexInGroup);
        if (clampedTarget == idx)
            return; // dropped back where it started — no-op, no undo entry pushed

        auto* moved = group[(size_t)idx];
        group.erase(group.begin() + idx);
        group.insert(group.begin() + clampedTarget, moved);

        // A drag can move a port an arbitrary number of places in one gesture, so renumber the
        // whole group sequentially rather than trying to patch individual `order` values.
        for (size_t i = 0; i < group.size(); ++i)
            group[i]->order = (int)i;
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doMove);
    else
        doMove();
}

juce::String MacroGroupController::changeMacroPortShape(const juce::String& macroId, const juce::String& nodeUuid,
                                                        MacroPortShape newShape, int newVoiceCount) {
    auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr)
        return {};

    synth::MacroPort* port = nullptr;
    for (auto& p : macro->ports)
        if (p.nodeUuid == nodeUuid) {
            port = &p;
            break;
        }
    if (port == nullptr || port->kind != synth::MacroPortKind::AudioCV)
        return {}; // MIDI ports have no shape to change (docs/macros/ports.md#node-types)

    auto& graph = host_.graph();
    const auto oldNodeId = resolveMemberNodeId(nodeUuid);
    auto* oldNode = graph.getNodeForId(oldNodeId);
    if (oldNode == nullptr)
        return {};

    const bool isInput = port->isInput;
    const juce::String typeName = macroPortNodeTypeName(isInput, synth::MacroPortKind::AudioCV);
    const juce::String portName = port->name;
    const int order = port->order;
    const int oldX = oldNode->properties.getWithDefault("x", 0);
    const int oldY = oldNode->properties.getWithDefault("y", 0);

    // Only a Mono->Stereo/StereoCollapsed grow needs auto-wiring below — every other
    // transition (shrinking, Poly<->anything) already got its exact intent from the modal's own
    // voice/shape controls and is left alone.
    MacroPortShape oldShape = MacroPortShape::Mono;
    if (auto* oldInlet = dynamic_cast<MacroInletModule*>(oldNode->getProcessor()))
        oldShape = oldInlet->getPortShape();
    else if (auto* oldOutlet = dynamic_cast<MacroOutletModule*>(oldNode->getProcessor()))
        oldShape = oldOutlet->getPortShape();

    // Snapshot every raw-channel connection touching the old node, replayed below onto the new
    // node's matching raw channel ONLY when the NEW shape still exposes that channel as a visible
    // jack (role == PortRole::Audio).
    struct SavedEdge {
        juce::AudioProcessorGraph::NodeID otherId;
        int otherChannel = 0;
        int myChannel = 0;
        bool oldNodeIsSource = false;
    };
    std::vector<SavedEdge> savedEdges;
    for (const auto& c : graph.getConnections()) {
        if (c.source.isMIDI() || c.destination.isMIDI())
            continue; // an AudioCV port never carries a MIDI edge
        if (c.source.nodeID == oldNodeId)
            savedEdges.push_back({c.destination.nodeID, c.destination.channelIndex, c.source.channelIndex, true});
        else if (c.destination.nodeID == oldNodeId)
            savedEdges.push_back({c.source.nodeID, c.source.channelIndex, c.destination.channelIndex, false});
    }

    juce::String newUuid;
    auto doChange = [this, macroId, nodeUuid, oldNodeId, typeName, newShape, newVoiceCount, portName, order, isInput,
                     savedEdges, oldX, oldY, oldShape, &newUuid] {
        auto& g = host_.graph();
        auto newProcessor = synth::AIStateMapper::createModule(typeName);
        if (!newProcessor)
            return;
        if (auto* inlet = dynamic_cast<MacroInletModule*>(newProcessor.get()))
            inlet->setPortShape(newShape, newVoiceCount);
        else if (auto* outlet = dynamic_cast<MacroOutletModule*>(newProcessor.get()))
            outlet->setPortShape(newShape, newVoiceCount);

        // Same cleanup replaceModule/deleteSelection do before a node leaves the graph.
        host_.clearModMatrixRows();
        g.removeNode(oldNodeId);

        auto node = g.addNode(std::move(newProcessor));
        if (!node)
            return;
        node->properties.set("x", oldX);
        node->properties.set("y", oldY);
        const juce::String uuid = synth::AIStateMapper::ensureNodeUuid(node);
        newUuid = uuid;

        auto* m = host_.getMacros().find(macroId);
        if (m != nullptr) {
            // Rewrite the OLD uuid to the NEW one everywhere it appeared, keeping name/order/
            // direction exactly as they were — the "one edit" the modal promises.
            for (auto& memberUuid : m->members)
                if (memberUuid == nodeUuid)
                    memberUuid = uuid;
            for (auto& p : m->ports)
                if (p.nodeUuid == nodeUuid) {
                    p.nodeUuid = uuid;
                    p.name = portName;
                    p.order = order;
                    p.isInput = isInput;
                    break;
                }
        }

        auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
        for (const auto& e : savedEdges) {
            if (g.getNodeForId(e.otherId) == nullptr)
                continue;
            const bool channelStillActive = mb != nullptr && mb->mapOutputChannel(e.myChannel).role == PortRole::Audio;
            if (!channelStillActive)
                continue;
            if (e.oldNodeIsSource)
                g.addConnection({{node->nodeID, e.myChannel}, {e.otherId, e.otherChannel}});
            else
                g.addConnection({{e.otherId, e.otherChannel}, {node->nodeID, e.myChannel}});
        }

        // A Mono->Stereo/StereoCollapsed grow adds a raw channel the OLD Mono port never
        // had, which the replay above leaves silently unwired — the expected result is the
        // same auto-wire the Dual I/O toggle gives an ordinary module that grows a right leg
        // (reachablePeerRightAudioLeg mirrors GraphEditor::rightAudioLegOf's rule for that). Every
        // savedEdge here is a Mono-era ch0 edge (the only raw channel Mono ever exposes), so each
        // one is a candidate; skip any defensively that isn't ch0 (shouldn't happen for an old
        // Mono port, but this loop must never assume it over checking).
        constexpr int kRightBase = MacroInletModule::kRightBase;
        static_assert(MacroOutletModule::kRightBase == kRightBase,
                      "MacroInletModule/MacroOutletModule must agree on the Stereo right-leg raw channel");
        if (oldShape == MacroPortShape::Mono &&
            (newShape == MacroPortShape::Stereo || newShape == MacroPortShape::StereoCollapsed)) {
            for (const auto& e : savedEdges) {
                if (e.myChannel != 0 || g.getNodeForId(e.otherId) == nullptr)
                    continue;
                auto* otherNode = g.getNodeForId(e.otherId);
                if (newShape == MacroPortShape::StereoCollapsed) {
                    // One visible jack, two raw legs: fan the SAME cable onto the hidden follower
                    // channel, exactly like a collapsed FX jack's own dual-raw-leg fan.
                    if (e.oldNodeIsSource)
                        g.addConnection({{node->nodeID, 1}, {e.otherId, e.otherChannel}});
                    else
                        g.addConnection({{e.otherId, e.otherChannel}, {node->nodeID, 1}});
                    continue;
                }
                // Stereo: two SEPARATE jacks. Pair with the peer's own right leg (L->L/R->R) when
                // it has one the user can reach; otherwise sum into the SAME mono jack the peer
                // already exposes — the "no second visible jack" ruling
                // completeStereoPairConnections applies for an ordinary Dual I/O toggle.
                const bool peerIsInput = e.oldNodeIsSource;
                const int peerLeg = reachablePeerRightAudioLeg(otherNode->getProcessor(), peerIsInput);
                const int peerChannel = peerLeg >= 0 ? peerLeg : e.otherChannel;
                if (e.oldNodeIsSource)
                    g.addConnection({{node->nodeID, kRightBase}, {e.otherId, peerChannel}});
                else
                    g.addConnection({{e.otherId, peerChannel}, {node->nodeID, kRightBase}});
            }
        }

        host_.updateComponents();
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doChange);
    else
        doChange();

    host_.requestRepaint();
    return newUuid;
}

void MacroGroupController::createMacroPortFromDroppedCable(const juce::String& macroId, bool newPortIsInput,
                                                           bool isMidi, juce::AudioProcessorGraph::NodeID otherNodeId,
                                                           int otherVisibleJack) {
    auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr)
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

    const juce::String name = defaultMacroPortName(newPortIsInput, kind);
    const int order = nextMacroPortOrder(*macro, newPortIsInput);

    // The new port's outward side lives in this macro's parent, so a cable to a node nested in some OTHER macro
    // crosses that node's own boundaries first: one port per boundary, innermost first, chained to the new port.
    // Every macro in this macro's own chain is shared ground, not a crossing.
    const auto& macros = host_.getMacros();
    auto sharedChain = macros.ancestorChain(macroId);
    sharedChain.insert(sharedChain.begin(), macroId);
    const auto otherCrossings = macro_nesting::boundariesCrossed(macros, nodeUuidFor(otherNodeId), sharedChain);

    auto& graph = host_.graph();
    auto proc = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(newProcessor));
    auto doCreate = [this, macroId, proc, placed, newPortIsInput, kind, name, order, isMidi, otherNodeId,
                     otherVisibleJack, otherCrossings] {
        auto& g = host_.graph();
        if (!*proc)
            return;
        auto node = g.addNode(std::move(*proc));
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
                const auto chainPort =
                    mintMacroPortForAutoCreate(crossedId, !newPortIsInput, isMidi, otherNodeId, otherVisibleJack);
                if (chainPort.uid == 0)
                    continue;
                if (newPortIsInput)
                    host_.connectPorts(effectiveOther, effectiveJack, chainPort, 0, isMidi, /*recordUndo=*/false);
                else
                    host_.connectPorts(chainPort, 0, effectiveOther, effectiveJack, isMidi, /*recordUndo=*/false);
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
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doCreate);
    else
        doCreate();

    host_.requestRepaint();
}

std::pair<MacroPortShape, int>
MacroGroupController::inferPortShapeFromCableFan(ModuleBase* otherMb, int otherVisibleJack, bool otherAsInput) {
    if (otherMb == nullptr || otherVisibleJack < 0)
        return {MacroPortShape::Mono, 1};

    // A jack can front more than one fan (Poly MIDI's single "Poly Out" carries both Pitch and
    // Gate) — same "widest span wins" read buildMacroPortCrossingPlan's own headSpan/headRole
    // derivation uses, so a mixed-role jack degrades to whichever fan is actually widest rather
    // than an arbitrary one.
    int headSpan = 1;
    PortRole headRole = PortRole::Other;
    for (const auto& t : otherMb->getJackTargets(otherVisibleJack, otherAsInput)) {
        if (t.voiceSpan > headSpan) {
            headSpan = t.voiceSpan;
            headRole = t.role;
        }
    }

    // A collapsed Key jack (PortRole::Sidechain, span 2) is the same stereo pair shape — the rule
    // buildMacroPortCrossingPlan applies at grouping time (MacroGroupControllerPortSplice.cpp).
    const bool stereoPairRole = headRole == PortRole::Audio || headRole == PortRole::Sidechain;
    if (headSpan > 1 && stereoPairRole)
        return {MacroPortShape::StereoCollapsed, 1}; // one jack, two raw legs — never the 2-jack Stereo shape
    if (headSpan > 1)
        return {MacroPortShape::Poly, headSpan};
    return {MacroPortShape::Mono, 1};
}

std::vector<synth::ui::MacroPortConfigDialog::PortRow>
MacroGroupController::macroPortRowsForDialog(const juce::String& macroId) const {
    std::vector<synth::ui::MacroPortConfigDialog::PortRow> rows;
    const auto* m = host_.getMacros().find(macroId);
    if (m == nullptr)
        return rows;

    auto ports = m->ports;
    // Inputs before outputs, then each side by its own draw order.
    std::sort(ports.begin(), ports.end(), [](const synth::MacroPort& a, const synth::MacroPort& b) {
        if (a.isInput != b.isInput)
            return a.isInput; // inputs (true) sort first
        return a.order < b.order;
    });

    auto& graph = host_.graph();
    for (const auto& p : ports) {
        synth::ui::MacroPortConfigDialog::PortRow row;
        row.nodeUuid = p.nodeUuid;
        row.isInput = p.isInput;
        row.name = p.name;
        row.kind = p.kind;
        row.colour = p.colour;
        if (p.kind == synth::MacroPortKind::AudioCV) {
            auto nodeId = resolveMemberNodeId(p.nodeUuid);
            if (auto* node = graph.getNodeForId(nodeId)) {
                if (auto* inlet = dynamic_cast<MacroInletModule*>(node->getProcessor())) {
                    row.shape = inlet->getPortShape();
                    row.voiceCount = inlet->getVoiceCount();
                } else if (auto* outlet = dynamic_cast<MacroOutletModule*>(node->getProcessor())) {
                    row.shape = outlet->getPortShape();
                    row.voiceCount = outlet->getVoiceCount();
                }
            }
        }
        rows.push_back(row);
    }
    return rows;
}
