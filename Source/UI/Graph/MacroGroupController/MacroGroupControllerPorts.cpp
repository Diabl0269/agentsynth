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
#include "Modules/MacroInletModule.h"
#include "Modules/MacroOutletModule.h"

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

    spliceOutMacroPort(*m, uuid);
    if (m->members.empty())
        host_.getMacros().remove(m->id); // MacroSet::removeMemberEverywhere's own "zero members" rule
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

    // Shape is set BEFORE the node is wired into the live graph, honouring §5.3's "decided at
    // construction, then fixed" rule — meaningless (and skipped) for a MIDI port (§5.1).
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
        if (m->members.empty())
            host_.getMacros().remove(macroId); // matches MacroSet::removeMemberEverywhere's own zero-members rule
        host_.updateComponents();
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doDelete);
    else
        doDelete();

    host_.requestRepaint();
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
        return {}; // MIDI ports have no shape to change (§5.1)

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
                     savedEdges, oldX, oldY, &newUuid] {
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
        // Always Mono — shape inference from the dragged cable's own poly/stereo fan is deferred;
        // the modal is how a Stereo or Poly-N port gets created.
        if (auto* inlet = dynamic_cast<MacroInletModule*>(newProcessor.get()))
            inlet->setPortShape(MacroPortShape::Mono, 1);
        else if (auto* outlet = dynamic_cast<MacroOutletModule*>(newProcessor.get()))
            outlet->setPortShape(MacroPortShape::Mono, 1);
    }

    // See addMacroPort's identical comment: dockMacroPortWidgets() (run from doCreate()'s own
    // updateComponents() below) places the widget for real.
    const auto placed = macro->bounds.getTopLeft();

    const juce::String name = defaultMacroPortName(newPortIsInput, kind);
    const int order = nextMacroPortOrder(*macro, newPortIsInput);

    auto& graph = host_.graph();
    auto proc = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(newProcessor));
    auto doCreate = [this, macroId, proc, placed, newPortIsInput, kind, name, order, isMidi, otherNodeId,
                     otherVisibleJack] {
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
            if (newPortIsInput)
                host_.connectPorts(otherNodeId, otherVisibleJack, node->nodeID, 0, isMidi, /*recordUndo=*/false);
            else
                host_.connectPorts(node->nodeID, 0, otherNodeId, otherVisibleJack, isMidi, /*recordUndo=*/false);
        }

        host_.updateComponents();
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(graph, host_.getMacros(), doCreate);
    else
        doCreate();

    host_.requestRepaint();
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
        row.colour = p.colour; // T152
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
