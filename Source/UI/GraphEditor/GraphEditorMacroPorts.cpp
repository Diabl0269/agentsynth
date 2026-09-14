// GraphEditorMacroPorts.cpp
//
// Macro port CRUD: auto-delete of orphaned ports, add/remove/rename/reorder/re-shape a macro
// port, creating a port from a dropped cable, and the "Configure I/O" dialog. GraphEditor is
// declared in GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest of
// the class.

#include "GraphEditor.h"

#include "../../AI/AIStateMapper.h"
#include "../../Modules/MacroInletModule.h"
#include "../../Modules/MacroOutletModule.h"
#include "../ModuleComponent/ModuleComponent.h"

void GraphEditor::autoDeleteOrphanedMacroPort(juce::AudioProcessorGraph::NodeID nodeId) {
    if (!autoDeleteMacroPortsOnLastCableEnabled)
        return;

    const juce::String uuid = nodeUuidFor(nodeId);
    if (uuid.isEmpty())
        return;
    auto* m = macros.findByMember(uuid);
    if (m == nullptr || !m->memberIsPort(uuid))
        return;

    auto& graph = audioEngine.getGraph();
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == nodeId || c.destination.nodeID == nodeId)
            return; // still has at least one cable — survives

    spliceOutMacroPort(*m, uuid);
    if (m->members.empty())
        macros.remove(m->id); // MacroSet::removeMemberEverywhere's own "zero members" rule
}

std::vector<juce::AudioProcessorGraph::NodeID>
GraphEditor::macroPortDeletionNeighbors(const std::vector<juce::AudioProcessorGraph::NodeID>& deletedIds) const {
    const auto isBeingDeleted = [&](juce::AudioProcessorGraph::NodeID id) {
        return std::find(deletedIds.begin(), deletedIds.end(), id) != deletedIds.end();
    };
    std::vector<juce::AudioProcessorGraph::NodeID> neighbors;
    for (const auto& c : audioEngine.getGraph().getConnections()) {
        if (isBeingDeleted(c.source.nodeID) && !isBeingDeleted(c.destination.nodeID))
            neighbors.push_back(c.destination.nodeID);
        else if (isBeingDeleted(c.destination.nodeID) && !isBeingDeleted(c.source.nodeID))
            neighbors.push_back(c.source.nodeID);
    }
    std::sort(neighbors.begin(), neighbors.end(), [](auto a, auto b) { return a.uid < b.uid; });
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    return neighbors;
}

juce::String GraphEditor::addMacroPort(const juce::String& macroId, bool isInput, synth::MacroPortKind kind,
                                       MacroPortShape shape, int voiceCount, const juce::String& portName) {
    auto* macro = macros.find(macroId);
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
    // dockMacroPortWidgets() at the end of every updateComponents() pass — including the one
    // doAdd() below calls. Stacking a fresh node below the (now nonexistent, once ports dock)
    // card via resolvePlacement() was the pre-F2 free-placement scheme; it is vestigial now, so
    // this just seeds a harmless position that the SAME updateComponents() call immediately
    // overrides — there is nothing left to resolvePlacement() against.
    const auto placed = macro->bounds.getTopLeft();

    const juce::String name = portName.trim().isNotEmpty() ? portName.trim() : defaultMacroPortName(isInput, kind);
    const int order = nextMacroPortOrder(*macro, isInput);

    auto& graph = audioEngine.getGraph();
    auto proc = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(newProcessor));
    juce::String newUuid;
    auto doAdd = [this, macroId, proc, placed, isInput, kind, name, order, &newUuid] {
        if (!*proc)
            return;
        auto node = audioEngine.getGraph().addNode(std::move(*proc));
        if (!node)
            return;
        node->properties.set("x", placed.x);
        node->properties.set("y", placed.y);
        const juce::String uuid = synth::AIStateMapper::ensureNodeUuid(node);
        newUuid = uuid;

        auto* m = macros.find(macroId);
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

        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doAdd);
    else
        doAdd();

    repaint();
    return newUuid;
}

void GraphEditor::removeMacroPort(const juce::String&, const juce::String& nodeUuid) {
    const auto nodeId = resolveMemberNodeId(nodeUuid);
    if (nodeId.uid == 0)
        return;

    // Reuses the ordinary multi-select delete path exactly, the same way deleteMacroAndMembers
    // reuses it for a whole macro: identical undo/dirty/timeline-reconcile handling, and
    // updateComponents() (run from inside deleteSelection's own recordGraphAndMacroChange
    // transaction) drops the now-orphaned synth::MacroPort via macros.retainOnly() as part of the
    // SAME undo step — never a separate one.
    setSelectedNodes({nodeId});
    deleteSelection();
}

void GraphEditor::deleteMacroPortNode(const juce::String& macroId, const juce::String& nodeUuid) {
    auto* macro = macros.find(macroId);
    if (macro == nullptr || !macro->memberIsPort(nodeUuid))
        return;

    auto& graph = audioEngine.getGraph();

    // Founder review, second pass: a port node had no delete affordance at all (Configure I/O's
    // own "delete this port" was the ONE surface, and it disappears the instant the macro does —
    // exactly when ungroup needs it most). This is the new one: the port node's own context menu
    // (ModuleComponent::buildMacroPortContextMenu). Splices the cable back — spliceOutMacroPort,
    // the same helper ungroupSelection() uses for every port of a dissolving macro — rather than
    // dropping it like removeMacroPort() above; see deleteMacroPortNode()'s own header comment for
    // why removeMacroPort's semantics are deliberately left alone.
    auto doDelete = [this, macroId, nodeUuid] {
        auto* m = macros.find(macroId);
        if (m == nullptr)
            return; // defensive: shouldn't happen mid-transaction
        spliceOutMacroPort(*m, nodeUuid);
        if (m->members.empty())
            macros.remove(macroId); // matches MacroSet::removeMemberEverywhere's own zero-members rule
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doDelete);
    else
        doDelete();

    repaint();
}

void GraphEditor::renameMacroPort(const juce::String& macroId, const juce::String& nodeUuid,
                                  const juce::String& newName) {
    const auto trimmed = newName.trim();
    if (trimmed.isEmpty())
        return; // empty/whitespace-only input cancels without renaming — promptRenameMacro's rule

    auto& graph = audioEngine.getGraph();
    auto doRename = [this, macroId, nodeUuid, trimmed] {
        if (auto* m = macros.find(macroId))
            for (auto& p : m->ports)
                if (p.nodeUuid == nodeUuid) {
                    p.name = trimmed;
                    break;
                }
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doRename);
    else
        doRename();
}

void GraphEditor::changeMacroPortColour(const juce::String& macroId, const juce::String& nodeUuid,
                                        std::optional<juce::Colour> newColour) {
    auto& graph = audioEngine.getGraph();
    auto doChange = [this, macroId, nodeUuid, newColour] {
        if (auto* m = macros.find(macroId))
            for (auto& p : m->ports)
                if (p.nodeUuid == nodeUuid) {
                    p.colour = newColour;
                    break;
                }
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doChange);
    else
        doChange();

    // Mirror setMacroColour: a macro-set change is not itself a graph change, so nothing repaints
    // the canvas on its own — force one so the new colour reaches both the collapsed card's jacks
    // and any expanded docked widget that fronts this port (T162's whole point).
    repaint();
}

void GraphEditor::moveMacroPortOrder(const juce::String& macroId, const juce::String& nodeUuid, bool moveUp) {
    auto& graph = audioEngine.getGraph();
    auto doMove = [this, macroId, nodeUuid, moveUp] {
        auto* m = macros.find(macroId);
        if (m == nullptr)
            return;

        auto selfIt = std::find_if(m->ports.begin(), m->ports.end(),
                                   [&](const synth::MacroPort& p) { return p.nodeUuid == nodeUuid; });
        if (selfIt == m->ports.end())
            return;
        const bool isInput = selfIt->isInput;

        // Reordering is scoped to one side of the card (inputs against inputs, outputs against
        // outputs) — the two independent jack stacks it will draw as (T141).
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

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doMove);
    else
        doMove();
}

void GraphEditor::reorderMacroPortToIndex(const juce::String& macroId, const juce::String& nodeUuid,
                                          int newIndexInGroup) {
    auto& graph = audioEngine.getGraph();
    auto doMove = [this, macroId, nodeUuid, newIndexInGroup] {
        auto* m = macros.find(macroId);
        if (m == nullptr)
            return;

        auto selfIt = std::find_if(m->ports.begin(), m->ports.end(),
                                   [&](const synth::MacroPort& p) { return p.nodeUuid == nodeUuid; });
        if (selfIt == m->ports.end())
            return;
        const bool isInput = selfIt->isInput;

        // Same one-side scoping moveMacroPortOrder uses above — there is no isInput parameter on
        // this method at all, which is what makes "can't drag an input into the output section"
        // structural rather than a value this function has to validate.
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

        // A drag can move a port an arbitrary number of places in one gesture (unlike moveUp/
        // moveDown's adjacent swap), so renumber the whole group sequentially rather than trying
        // to patch individual `order` values — the only way to GUARANTEE a consistent, gap-free
        // order after an arbitrary-distance move.
        for (size_t i = 0; i < group.size(); ++i)
            group[i]->order = (int)i;
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doMove);
    else
        doMove();
}

juce::String GraphEditor::changeMacroPortShape(const juce::String& macroId, const juce::String& nodeUuid,
                                               MacroPortShape newShape, int newVoiceCount) {
    auto* macro = macros.find(macroId);
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

    auto& graph = audioEngine.getGraph();
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
    // jack (role == PortRole::Audio). Everything else is dropped — exactly
    // dropRoutingsOnHiddenJacks' "a jack that disappears takes its cables with it" rule, applied
    // honestly here rather than silently adapted at the boundary (§5.3's closing rule: a mismatch
    // is refused, never adapted).
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
        auto& g = audioEngine.getGraph();
        auto newProcessor = synth::AIStateMapper::createModule(typeName);
        if (!newProcessor)
            return;
        if (auto* inlet = dynamic_cast<MacroInletModule*>(newProcessor.get()))
            inlet->setPortShape(newShape, newVoiceCount);
        else if (auto* outlet = dynamic_cast<MacroOutletModule*>(newProcessor.get()))
            outlet->setPortShape(newShape, newVoiceCount);

        // Same cleanup replaceModule/deleteSelection do before a node leaves the graph.
        modMatrix.clearRows();
        g.removeNode(oldNodeId);

        auto node = g.addNode(std::move(newProcessor));
        if (!node)
            return;
        node->properties.set("x", oldX);
        node->properties.set("y", oldY);
        const juce::String uuid = synth::AIStateMapper::ensureNodeUuid(node);
        newUuid = uuid;

        auto* m = macros.find(macroId);
        if (m != nullptr) {
            // Rewrite the OLD uuid to the NEW one everywhere it appeared, keeping name/order/
            // direction exactly as they were — the "one edit" the modal promises, not a delete
            // followed by a fresh add that merely happens to look the same.
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

        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doChange);
    else
        doChange();

    repaint();
    return newUuid;
}

void GraphEditor::createMacroPortFromDroppedCable(const juce::String& macroId, bool newPortIsInput, bool isMidi,
                                                  juce::AudioProcessorGraph::NodeID otherNodeId, int otherVisibleJack) {
    auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return;

    const auto kind = isMidi ? synth::MacroPortKind::Midi : synth::MacroPortKind::AudioCV;
    const juce::String typeName = macroPortNodeTypeName(newPortIsInput, kind);
    auto newProcessor = synth::AIStateMapper::createModule(typeName);
    if (!newProcessor)
        return;
    if (!isMidi) {
        // Always Mono — shape inference from the dragged cable's own poly/stereo fan is deferred
        // (see the class comment on createMacroPortFromDroppedCable); the modal is how a Stereo or
        // Poly-N port gets created.
        if (auto* inlet = dynamic_cast<MacroInletModule*>(newProcessor.get()))
            inlet->setPortShape(MacroPortShape::Mono, 1);
        else if (auto* outlet = dynamic_cast<MacroOutletModule*>(newProcessor.get()))
            outlet->setPortShape(MacroPortShape::Mono, 1);
    }

    // See addMacroPort's identical comment: dockMacroPortWidgets() (run from doCreate()'s own
    // updateComponents() below) places the widget for real — this only seeds a harmless position
    // for the instant before that.
    const auto placed = macro->bounds.getTopLeft();

    const juce::String name = defaultMacroPortName(newPortIsInput, kind);
    const int order = nextMacroPortOrder(*macro, newPortIsInput);

    auto& graph = audioEngine.getGraph();
    auto proc = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(newProcessor));
    auto doCreate = [this, macroId, proc, placed, newPortIsInput, kind, name, order, isMidi, otherNodeId,
                     otherVisibleJack] {
        auto& g = audioEngine.getGraph();
        if (!*proc)
            return;
        auto node = g.addNode(std::move(*proc));
        if (!node)
            return;
        node->properties.set("x", placed.x);
        node->properties.set("y", placed.y);
        const juce::String uuid = synth::AIStateMapper::ensureNodeUuid(node);

        auto* m = macros.find(macroId);
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

        // Wire the boundary through the SAME connectPorts a completed manual cable-drag uses —
        // visible-jack-to-visible-jack, so poly/stereo fan resolution and the "mismatched
        // connection is refused, not adapted" rule (§5.3) are the existing behaviour, not a copy
        // of it. The new node's port 0 is its only visible jack (Mono, or MIDI's one jack).
        // recordUndo=false: already inside this method's own recordGraphAndMacroChange
        // transaction (applySmartSuggestions' own comment states the same rule). Deliberately does
        // NOT also wire anything on the INTERIOR side — see the class comment.
        if (g.getNodeForId(otherNodeId) != nullptr) {
            if (newPortIsInput)
                connectPorts(otherNodeId, otherVisibleJack, node->nodeID, 0, isMidi, /*recordUndo=*/false);
            else
                connectPorts(node->nodeID, 0, otherNodeId, otherVisibleJack, isMidi, /*recordUndo=*/false);
        }

        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doCreate);
    else
        doCreate();

    repaint();
}

std::vector<synth::ui::MacroPortConfigDialog::PortRow>
GraphEditor::macroPortRowsForDialog(const juce::String& macroId) const {
    std::vector<synth::ui::MacroPortConfigDialog::PortRow> rows;
    const auto* m = macros.find(macroId);
    if (m == nullptr)
        return rows;

    auto ports = m->ports;
    // Inputs before outputs, then each side by its own draw order — matches how a normal module's
    // jacks read (inputs on the left, outputs on the right) and what the row-reorder buttons above
    // operate on.
    std::sort(ports.begin(), ports.end(), [](const synth::MacroPort& a, const synth::MacroPort& b) {
        if (a.isInput != b.isInput)
            return a.isInput; // inputs (true) sort first
        return a.order < b.order;
    });

    auto& graph = audioEngine.getGraph();
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

void GraphEditor::promptConfigureMacroIO(const juce::String& macroId) {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return;

    auto* dialog = new synth::ui::MacroPortConfigDialog(macro->name, macroPortRowsForDialog(macroId));
    dialog->setColourPickerPropertiesFile(propertiesFile_); // T152; nullptr is fine (in-memory favs)

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = "Configure I/O";
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    // T153: same reasoning as showMacroAutoPortModal above — the dialog's own keyPressed()
    // override (Escape -> onRequestClose) is the ONE Escape route, not competing with
    // juce::DialogWindow's default (which would just hide the window on a different dispatch
    // path, bypassing onRequestClose and every commit-on-close side effect it triggers).
    options.escapeKeyTriggersCloseButton = false;
    auto* window = options.launchAsync();

    juce::Component::SafePointer<GraphEditor> safeThis(this);
    juce::Component::SafePointer<synth::ui::MacroPortConfigDialog> safeDialog(dialog);

    dialog->onRequestClose = [window] {
        if (window != nullptr)
            window->exitModalState(0);
    };

    // Every callback below defers its mutate-then-refresh to the next message-loop tick — a
    // button's onClick handler calling refreshPorts() synchronously would tear down and rebuild
    // the very row (and button) that is still inside its own click dispatch, which JUCE does not
    // support. callAsync sidesteps that exactly like AIChatComponent/ModuleComponent already do
    // for the same reason.
    dialog->onAddPort = [safeThis, safeDialog, macroId](bool isInput, synth::MacroPortKind kind, MacroPortShape shape,
                                                        int voiceCount, const juce::String& name) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, isInput, kind, shape, voiceCount, name] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->addMacroPort(macroId, isInput, kind, shape, voiceCount, name);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };
    dialog->onRenamePort = [safeThis, safeDialog, macroId](const juce::String& nodeUuid, const juce::String& name) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, nodeUuid, name] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->renameMacroPort(macroId, nodeUuid, name);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };
    dialog->onDeletePort = [safeThis, safeDialog, macroId](const juce::String& nodeUuid) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, nodeUuid] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->removeMacroPort(macroId, nodeUuid);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };
    dialog->onReorderPort = [safeThis, safeDialog, macroId](const juce::String& nodeUuid, bool moveUp) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, nodeUuid, moveUp] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->moveMacroPortOrder(macroId, nodeUuid, moveUp);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };
    dialog->onReorderPortTo = [safeThis, safeDialog, macroId](const juce::String& nodeUuid, int newIndexInGroup) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, nodeUuid, newIndexInGroup] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->reorderMacroPortToIndex(macroId, nodeUuid, newIndexInGroup);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };
    dialog->onChangePortShape = [safeThis, safeDialog, macroId](const juce::String& nodeUuid, MacroPortShape newShape,
                                                                int newVoiceCount) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, nodeUuid, newShape, newVoiceCount] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->changeMacroPortShape(macroId, nodeUuid, newShape, newVoiceCount);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };
    dialog->onChangePortColour = [safeThis, safeDialog, macroId](const juce::String& nodeUuid,
                                                                 std::optional<juce::Colour> newColour) {
        juce::MessageManager::callAsync([safeThis, safeDialog, macroId, nodeUuid, newColour] {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;
            self->changeMacroPortColour(macroId, nodeUuid, newColour);
            if (auto* d = safeDialog.getComponent())
                d->refreshPorts(self->macroPortRowsForDialog(macroId));
        });
    };

    window->enterModalState(true, nullptr, true);
}

void GraphEditor::promptRenameMacroPort(const juce::String& macroId, const juce::String& nodeUuid) {
    const auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return;

    juce::String currentName;
    for (const auto& p : macro->ports)
        if (p.nodeUuid == nodeUuid)
            currentName = p.name;

    // promptRenameMacro's own AlertWindow idiom exactly (see its comment for the ownership/
    // lifetime reasoning) — the port node's own context menu's quicker alternative to opening the
    // whole Configure I/O modal just to retype one name.
    auto* window = new juce::AlertWindow("Rename Port", "New name:", juce::AlertWindow::NoIcon);
    window->addTextEditor("name", currentName, "Port name:");
    window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<GraphEditor> safeThis(this);
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([safeThis, window, macroId, nodeUuid](int result) {
                                std::unique_ptr<juce::AlertWindow> owned(window);
                                if (result != 1)
                                    return;

                                auto* self = safeThis.getComponent();
                                if (self == nullptr)
                                    return;

                                const auto typed = owned->getTextEditorContents("name").trim();
                                if (typed.isEmpty())
                                    return; // empty/whitespace-only input cancels without renaming

                                self->renameMacroPort(macroId, nodeUuid, typed);
                            }),
                            false);
}
