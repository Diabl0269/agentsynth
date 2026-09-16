// GraphEditorCommands.cpp
//
// Snippets, copy/paste/duplicate, the canvas context menu, keyboard handling, delete/replace
// module, disconnect port, poly-change rewiring, and the animation timer callback. GraphEditor
// is declared in GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest
// of the class.

#include "GraphEditor.h"
#include "GraphEditorInternal.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/AttenuverterModule.h"
#include "SnippetManager.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

using namespace detail;

// ---- Snippets ----

juce::Point<int> GraphEditor::estimateSnippetSize(const juce::String& payload) const {
    // Fallback footprint when the snippet can't be resolved or carries no placeable nodes.
    const juce::Point<int> fallback{synth::LayoutUtil::kSingleWidth, 200};

    if (!snippetProvider)
        return fallback;

    auto snippet = snippetProvider(synth::SnippetManager::nameFromPayload(payload));
    auto* obj = snippet.getDynamicObject();
    if (obj == nullptr || !obj->hasProperty("nodes"))
        return fallback;
    auto* nodes = obj->getProperty("nodes").getArray();
    if (nodes == nullptr || nodes->isEmpty())
        return fallback;

    // Snippet positions are already origin-relative, so the union of every node's estimated box
    // from (0,0) is the group footprint.
    juce::Rectangle<int> bounds;
    for (const auto& nVar : *nodes) {
        auto* nObj = nVar.getDynamicObject();
        if (nObj == nullptr)
            continue;

        juce::Point<int> pos;
        if (auto* posObj = nObj->getProperty("position").getDynamicObject()) {
            pos = {(int)posObj->getProperty("x"), (int)posObj->getProperty("y")};
        }
        auto size = estimateModuleSize(nObj->getProperty("type").toString());
        juce::Rectangle<int> box(pos.x, pos.y, size.x, size.y);
        bounds = bounds.isEmpty() ? box : bounds.getUnion(box);
    }

    return bounds.isEmpty() ? fallback : juce::Point<int>(bounds.getWidth(), bounds.getHeight());
}

juce::var GraphEditor::extractSelectionSnippet(const juce::String& name) {
    return synth::SnippetManager::extractSnippet(audioEngine.getGraph(), selection.getSelected(), name,
                                                 /*includeExtraState=*/false, macros);
}

bool GraphEditor::insertSnippetAt(const juce::var& snippet, juce::Point<int> canvasPos) {
    auto& graph = audioEngine.getGraph();

    // Snap the drop point so an inserted group lands on the same grid as everything else. The
    // snippet's own internal offsets are preserved relative to it.
    auto dropPos = synth::LayoutUtil::snap(canvasPos);

    std::vector<juce::AudioProcessorGraph::NodeID> added;
    std::vector<synth::Macro> addedMacros;
    auto doInsert = [this, &graph, &snippet, dropPos, &added, &addedMacros] {
        added =
            synth::SnippetManager::insertSnippet(snippet, graph, dropPos, /*includeExtraState=*/false, &addedMacros);
        for (auto& macro : addedMacros)
            macros.add(macro);
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doInsert);
    else
        doInsert();

    if (added.empty())
        return false;

    // Leave the freshly inserted group selected: it is what the user will want to move next.
    applySelectionChange(added);
    repaint();
    return true;
}

// ---- Copy / paste / duplicate ----

bool GraphEditor::insertClipboardPayload(const juce::var& payload, juce::Point<int> canvasPos) {
    auto& graph = audioEngine.getGraph();
    auto dropPos = synth::LayoutUtil::snap(canvasPos);

    std::vector<juce::AudioProcessorGraph::NodeID> added;
    std::vector<synth::Macro> addedMacros;
    auto doInsert = [this, &graph, &payload, dropPos, &added, &addedMacros] {
        // includeExtraState: the payload came from the live graph in this session, so carrying a
        // Sampler's loaded file or a Wavetable's custom table through is both safe and expected —
        // a duplicated Sampler that lost its sample would not be a duplicate.
        added = synth::SnippetManager::insertSnippet(payload, graph, dropPos, /*includeExtraState=*/true, &addedMacros);
        for (auto& macro : addedMacros)
            macros.add(macro);
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doInsert);
    else
        doInsert();

    if (added.empty())
        return false;

    // Leave the copies selected, not the originals: the group the user just made is what they will
    // want to drag, delete or duplicate again.
    applySelectionChange(added);
    repaint();
    return true;
}

bool GraphEditor::copySelection() {
    auto ids = selection.getSelected();
    if (ids.empty())
        return false;

    auto payload = synth::SnippetManager::extractSnippet(audioEngine.getGraph(), ids, "Clipboard",
                                                         /*includeExtraState=*/true, macros);
    if (synth::SnippetManager::getModuleCount(payload) <= 0)
        return false; // nothing eligible (e.g. only Audio Output was selected) — keep what we had

    clipboard.set(payload, synth::SnippetManager::selectionOrigin(audioEngine.getGraph(), ids));
    return true;
}

bool GraphEditor::pasteClipboard() {
    if (clipboard.isEmpty())
        return false;

    // Take the payload by value first: the cascade advances even if the insert is rejected, which
    // is the right behaviour — a failed paste must not leave the next one aimed at the same spot.
    const auto payload = clipboard.getPayload();
    return insertClipboardPayload(payload, clipboard.nextPastePosition());
}

bool GraphEditor::pasteClipboardAt(juce::Point<int> canvasPos) {
    if (clipboard.isEmpty())
        return false;

    const auto payload = clipboard.getPayload();
    clipboard.anchorAt(canvasPos);
    return insertClipboardPayload(payload, canvasPos);
}

bool GraphEditor::duplicateSelection() {
    auto ids = selection.getSelected();
    if (ids.empty())
        return false;

    auto& graph = audioEngine.getGraph();
    auto payload = synth::SnippetManager::extractSnippet(graph, ids, "Duplicate", /*includeExtraState=*/true, macros);
    if (synth::SnippetManager::getModuleCount(payload) <= 0)
        return false;

    const auto origin = synth::SnippetManager::selectionOrigin(graph, ids);
    const int step = synth::ui::ModuleClipboard::kOffsetStep;
    return insertClipboardPayload(payload, origin + juce::Point<int>(step, step));
}

void GraphEditor::showCanvasContextMenu(juce::Point<int> canvasPos) {
    juce::Component::SafePointer<GraphEditor> safeThis(this);

    juce::PopupMenu m;
    const int clipboardCount = clipboard.getModuleCount();
    juce::PopupMenu::Item paste(clipboardCount > 1 ? "Paste " + juce::String(clipboardCount) + " Modules Here"
                                                   : "Paste Here");
    paste.setEnabled(clipboardCount > 0);
    paste.action = [safeThis, canvasPos] {
        if (safeThis != nullptr)
            safeThis->pasteClipboardAt(canvasPos);
    };
    m.addItem(paste);

    m.addSeparator();
    m.addItem("Select All Modules", [safeThis] {
        if (safeThis != nullptr)
            safeThis->selectAllModules();
    });

    // FRO45: discoverable regardless of where auto-arrange or a drag left Master/Audio Output.
    // Disabled (mirroring the Paste item's setEnabled idiom above) rather than hidden, so the row
    // stays in a stable place whether or not the patch has a channel yet.
    juce::PopupMenu::Item locateMaster("Locate Master");
    locateMaster.setEnabled(hasLocatableMasterOrOutput());
    locateMaster.action = [safeThis] {
        if (safeThis != nullptr)
            safeThis->locateMasterOrOutput();
    };
    m.addItem(locateMaster);

    const int selectionCount = getSelectionCount();
    if (selectionCount > 1) {
        // Calls requestGroupSelectionIntoMacro() directly, not the Cmd+G dispatch — see the
        // matching comment in ModuleComponent.cpp's right-click menu for why. That entry point
        // gates the auto-port-preference modal (founder-review fix F5, docs/macros_implementation.md §7 item
        // 6.2) the same way Cmd+G does.
        m.addItem("Create Macro from " + juce::String(selectionCount) + " Modules", [safeThis] {
            if (safeThis != nullptr)
                safeThis->requestGroupSelectionIntoMacro();
        });
    }

    // FRO25 (P9-3d): "Make Channel" for the selected chain (see addMakeChannelMenuItem).
    if (selectionCount > 0)
        addMakeChannelMenuItem(m);

    if (showCanvasContextMenuHook_) {
        showCanvasContextMenuHook_(m);
        return;
    }
    m.showMenuAsync(juce::PopupMenu::Options());
}

bool GraphEditor::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (selection.isEmpty())
            return false;
        clearSelection();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (selection.isEmpty())
            return false;
        deleteSelection();
        return true;
    }

    return false;
}

void GraphEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    auto localPos = content.getLocalPoint(this, e.getPosition());

    // Double-clicking an expanded macro's name chip renames it - mirrors the collapsed card's own
    // double-click-to-rename affordance.
    if (auto macroId = macroChipAt(localPos.roundToInt()); macroId.isNotEmpty()) {
        // JUCE delivers mouseDown before mouseDoubleClick, so the second click already armed a
        // chip drag (see mouseDown). The modal rename AlertWindow below can swallow the mouseUp
        // that would otherwise clear it, leaving macroChipDragId stuck non-empty and the next drag
        // anywhere on empty canvas moving this macro instead of panning - drop it unconditionally
        // rather than depending on that mouseUp ever arriving.
        if (macroChipDragId.isNotEmpty()) {
            cancelSelectionDrag();
            macroChipDragId.clear();
        }
        promptRenameMacro(macroId);
        return;
    }

    auto attenId = getAttenuverterNodeAt(localPos.toFloat());
    if (attenId.uid != 0) {
        if (undoManager) {
            undoManager->recordStructuralChange(audioEngine.getGraph(),
                                                [this, attenId] { audioEngine.removeModRouting(attenId); });
        } else {
            audioEngine.removeModRouting(attenId);
        }
        repaintCanvas();
    }
}

juce::AudioProcessorGraph::NodeID GraphEditor::getAttenuverterNodeAt(juce::Point<float> localPos) {
    // Reuses buildVisibleCables()'s own AttenuverterChain geometry (T144) rather than re-deriving
    // source/dest positions from raw graph connections: that duplicate computation used the raw
    // channel index (not mapOutputChannel/mapInputChannel's visible-jack mapping) and never ran the
    // collapsed-macro re-anchoring pass, so it silently drifted off the actually-painted knob the
    // moment either endpoint was a macro port node -- exactly the two-macro crossing this hit-test
    // exists to serve. See the class comment above CableId/VisibleCable for why a cable's identity
    // and geometry must only ever come from this one place.
    for (const auto& cable : buildVisibleCables()) {
        if (cable.kind != VisibleCable::Kind::AttenuverterChain)
            continue;
        const auto mid = (cable.p1 + cable.p2) / 2.0f;
        if (mid.getDistanceFrom(localPos) <= 15.0f)
            return juce::AudioProcessorGraph::NodeID{cable.id.attenUid};
    }
    return {};
}

void GraphEditor::updateModulePosition(ModuleComponent* module) {
    if (!module)
        return;
    for (auto* n : audioEngine.getGraph().getNodes()) {
        if (n->getProcessor() == module->getModule()) {
            n->properties.set("x", module->getX());
            n->properties.set("y", module->getY());
            break;
        }
    }
}

void GraphEditor::deleteModule(ModuleComponent* module) {
    auto& graph = audioEngine.getGraph();
    juce::AudioProcessorGraph::NodeID nodeId;

    for (auto* n : graph.getNodes()) {
        if (n->getProcessor() == module->getModule()) {
            nodeId = n->nodeID;
            break;
        }
    }

    // Resolve NodeID then delegate to the single removal path.
    requestDeleteModule(nodeId);
}

void GraphEditor::requestDeleteModule(juce::AudioProcessorGraph::NodeID nodeId) {
    if (nodeId.uid == 0)
        return;

    auto& graph = audioEngine.getGraph();

    // updateComponents() below prunes `macros` against whatever nodes survive, so a module that
    // was a macro member either shrinks or dissolves its macro as part of the SAME undo step —
    // recordGraphAndMacroChange captures both "before"/"after" snapshots, not just the graph's.
    // T154: deleting this single node can also strand a DIFFERENT macro port wired only to it —
    // see macroPortDeletionNeighbors()'s comment. deleteModule(ModuleComponent*) resolves a NodeID
    // and delegates here, so it's covered too.
    auto doDelete = [this, nodeId, &graph] {
        modMatrix.clearRows();
        const auto portNeighbors = macroPortDeletionNeighbors({nodeId}); // T154: capture BEFORE removal
        // graph.removeNode() frees this node's processor -- and its AudioProcessorParameters --
        // synchronously, and a module card's own Delete is just as able to remove a
        // ChannelStripModule/MasterModule as a canvas "Delete" is. Nothing on this path rebuilds
        // the mixer either (updateComponents() only reaches reconcileTimelineBindingsOnly(),
        // which deliberately never does), so a mixer column's fader/pan/send-row bindings into
        // this node would dangle until an unrelated later graph edit finally dereferenced them.
        // Same seam, same pre-removal ordering as GraphEditor::deleteSelection().
        if (onBeforeDetachAllModuleComponents)
            onBeforeDetachAllModuleComponents();
        graph.removeNode(nodeId);
        for (auto n : portNeighbors)
            autoDeleteOrphanedMacroPort(n);
        updateComponents();
    };

    if (undoManager)
        undoManager->recordGraphAndMacroChange(graph, macros, doDelete);
    else
        doDelete();
    repaint();
}

void GraphEditor::replaceModule(ModuleComponent* moduleComp, const juce::String& newModuleType) {
    auto& graph = audioEngine.getGraph();

    // Find the old node by processor pointer (same pattern as deleteModule)
    juce::AudioProcessorGraph::NodeID oldNodeId;
    for (auto* n : graph.getNodes()) {
        if (n->getProcessor() == moduleComp->getModule()) {
            oldNodeId = n->nodeID;
            break;
        }
    }
    if (oldNodeId.uid == 0)
        return;

    // Use shared_ptr to make the lambda copyable (std::function requires it)
    auto newModuleTypeCopy = newModuleType;

    auto doReplace = [this, &graph, oldNodeId, newModuleTypeCopy] {
        auto* oldNode = graph.getNodeForId(oldNodeId);
        if (!oldNode)
            return;

        // 1. Create the new module
        auto newProcessor = synth::AIStateMapper::createModule(newModuleTypeCopy);
        if (!newProcessor)
            return;

        // 2. Snapshot old module's properties
        int posX = oldNode->properties.getWithDefault("x", 0);
        int posY = oldNode->properties.getWithDefault("y", 0);
        auto* oldProc = oldNode->getProcessor();
        int oldNumInputs = oldProc->getTotalNumInputChannels();
        int oldNumOutputs = oldProc->getTotalNumOutputChannels();
        bool oldAcceptsMidi = oldProc->acceptsMidi();
        bool oldProducesMidi = oldProc->producesMidi();

        // 3. Get new module capabilities before addNode moves it
        int newNumInputs = newProcessor->getTotalNumInputChannels();
        int newNumOutputs = newProcessor->getTotalNumOutputChannels();
        bool newAcceptsMidi = newProcessor->acceptsMidi();
        bool newProducesMidi = newProcessor->producesMidi();

        // 4. Collect all connections involving the old node
        struct ConnectionInfo {
            juce::AudioProcessorGraph::NodeID otherNodeId;
            int otherChannelIndex;
            int oldChannelIndex;
            bool isIncoming; // true = other->old, false = old->other
            bool isMidi;
        };
        std::vector<ConnectionInfo> directConnections;

        struct ModRoutingRewire {
            juce::AudioProcessorGraph::NodeID attenuverterId;
            int channelOnOldModule;
            bool oldModuleIsSource;
        };
        std::vector<ModRoutingRewire> modRoutings;

        for (auto& conn : graph.getConnections()) {
            bool srcIsOld = (conn.source.nodeID == oldNodeId);
            bool dstIsOld = (conn.destination.nodeID == oldNodeId);
            if (!srcIsOld && !dstIsOld)
                continue;

            // Check if this involves an attenuverter
            if (srcIsOld) {
                auto* dstNode = graph.getNodeForId(conn.destination.nodeID);
                if (dstNode && dynamic_cast<AttenuverterModule*>(dstNode->getProcessor())) {
                    ModRoutingRewire rw;
                    rw.attenuverterId = conn.destination.nodeID;
                    rw.channelOnOldModule = conn.source.channelIndex;
                    rw.oldModuleIsSource = true;
                    modRoutings.push_back(rw);
                    continue;
                }
            }
            if (dstIsOld) {
                auto* srcNode = graph.getNodeForId(conn.source.nodeID);
                if (srcNode && dynamic_cast<AttenuverterModule*>(srcNode->getProcessor())) {
                    ModRoutingRewire rw;
                    rw.attenuverterId = conn.source.nodeID;
                    rw.channelOnOldModule = conn.destination.channelIndex;
                    rw.oldModuleIsSource = false;
                    modRoutings.push_back(rw);
                    continue;
                }
            }

            // Direct connection (not attenuverter-mediated)
            ConnectionInfo ci;
            bool isMidiConn =
                (srcIsOld && conn.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex) ||
                (dstIsOld && conn.destination.channelIndex == juce::AudioProcessorGraph::midiChannelIndex);
            ci.isMidi = isMidiConn;
            if (srcIsOld) {
                ci.otherNodeId = conn.destination.nodeID;
                ci.otherChannelIndex = conn.destination.channelIndex;
                ci.oldChannelIndex = conn.source.channelIndex;
                ci.isIncoming = false;
            } else {
                ci.otherNodeId = conn.source.nodeID;
                ci.otherChannelIndex = conn.source.channelIndex;
                ci.oldChannelIndex = conn.destination.channelIndex;
                ci.isIncoming = true;
            }
            directConnections.push_back(ci);
        }

        // 5. Add the new node to the graph
        auto newNode = graph.addNode(std::move(newProcessor));
        if (!newNode)
            return;
        auto newNodeId = newNode->nodeID;
        newNode->properties.set("x", posX);
        newNode->properties.set("y", posY);

        // 6. Remove the old node (this removes all its connections)
        modMatrix.clearRows();
        // removeNode() frees the old processor and its AudioProcessorParameters synchronously.
        // "Replace with..." is offered for every module except the singleton Audio Input/Output,
        // so the node being replaced can be the very ChannelStripModule/MasterModule a mixer
        // column's fader, pan attachment or send rows are bound to -- and nothing on this path
        // rebuilds the mixer afterwards (updateComponents() only reaches
        // reconcileTimelineBindingsOnly(), which deliberately never does), so those bindings
        // would dangle until an unrelated later graph edit dereferenced them. Same seam and same
        // pre-removal ordering as GraphEditor::deleteSelection().
        //
        // This sits here rather than at the top of doReplace deliberately: every early return
        // above (unknown module type, addNode failure) leaves the graph untouched, and unbinding
        // the whole mixer for a replace that never happened would leave every fader inert until
        // the next rebuild. Nothing between the checks above and this line touches a mixer
        // column -- steps 1-5 read only the old node and the graph -- so unbinding here is no
        // later, in ordering terms, than unbinding there.
        if (onBeforeDetachAllModuleComponents)
            onBeforeDetachAllModuleComponents();
        graph.removeNode(oldNodeId);

        // 7. Re-create compatible direct connections
        for (auto& ci : directConnections) {
            if (ci.isMidi) {
                if (ci.isIncoming && newAcceptsMidi) {
                    graph.addConnection({{ci.otherNodeId, juce::AudioProcessorGraph::midiChannelIndex},
                                         {newNodeId, juce::AudioProcessorGraph::midiChannelIndex}});
                } else if (!ci.isIncoming && newProducesMidi) {
                    graph.addConnection({{newNodeId, juce::AudioProcessorGraph::midiChannelIndex},
                                         {ci.otherNodeId, juce::AudioProcessorGraph::midiChannelIndex}});
                }
            } else {
                if (ci.isIncoming && ci.oldChannelIndex < newNumInputs) {
                    graph.addConnection({{ci.otherNodeId, ci.otherChannelIndex}, {newNodeId, ci.oldChannelIndex}});
                } else if (!ci.isIncoming && ci.oldChannelIndex < newNumOutputs) {
                    graph.addConnection({{newNodeId, ci.oldChannelIndex}, {ci.otherNodeId, ci.otherChannelIndex}});
                }
            }
        }

        // 8. Re-create compatible modulation routings
        // removeNode(oldNodeId) only removes connections TO/FROM oldNodeId.
        // For mod routings: source->attenuverter->dest
        // If old was source: old->atten connection is removed, atten->dest survives
        // If old was dest: atten->old connection is removed, source->atten survives
        // We only need to re-add the destroyed leg.
        for (auto& rw : modRoutings) {
            if (rw.oldModuleIsSource) {
                if (rw.channelOnOldModule < newNumOutputs) {
                    graph.addConnection({{newNodeId, rw.channelOnOldModule}, {rw.attenuverterId, 0}});
                }
            } else {
                if (rw.channelOnOldModule < newNumInputs) {
                    graph.addConnection({{rw.attenuverterId, 0}, {newNodeId, rw.channelOnOldModule}});
                }
            }
        }

        // 9. Refresh UI
        updateComponents();
        audioEngine.updateModuleNames();
    };

    if (undoManager) {
        undoManager->recordStructuralChange(graph, doReplace);
    } else {
        doReplace();
    }
    repaint();
}

void GraphEditor::disconnectPort(ModuleComponent* module, int portIndex, bool isInput, bool isMidi) {
    auto& graph = audioEngine.getGraph();
    juce::AudioProcessorGraph::NodeID nodeId;

    for (auto* n : graph.getNodes()) {
        if (n->getProcessor() == module->getModule()) {
            nodeId = n->nodeID;
            break;
        }
    }

    if (nodeId.uid == 0)
        return;

    // A visible jack can front an N-voice fan (and Poly MIDI's single jack fronts two), so gather every
    // raw channel it owns — otherwise "Disconnect" would leave 7 of 8 voices still wired.
    std::vector<int> targetChannels;
    if (isMidi) {
        targetChannels.push_back(juce::AudioProcessorGraph::midiChannelIndex);
    } else if (auto* modBase = dynamic_cast<ModuleBase*>(module->getModule())) {
        for (const auto& t : modBase->getJackTargets(portIndex, isInput))
            for (int v = 0; v < t.voiceSpan; ++v)
                targetChannels.push_back(t.rawHeadChannel + v);
    } else {
        targetChannels.push_back(portIndex);
    }

    // T148 (docs/macros_implementation.md §7 item 9): decide BEFORE mutating whether this disconnect can leave a
    // macro port cableless — nodeId's own jack, or the far end of any plain (non-attenuverter)
    // connection about to be removed. Only then does the transaction upgrade to
    // recordGraphAndMacroChange; an ordinary disconnect keeps the existing graph-only
    // recordStructuralChange path exactly as before. Gated on
    // autoDeleteMacroPortsOnLastCableEnabled (Preferences) — off, this is always false.
    bool touchesMacroPort = autoDeleteMacroPortsOnLastCableEnabled && nodeIsMacroPort(nodeId);
    if (autoDeleteMacroPortsOnLastCableEnabled && !touchesMacroPort) {
        auto isTargetChannelPrescan = [&targetChannels](int channel) {
            return std::find(targetChannels.begin(), targetChannels.end(), channel) != targetChannels.end();
        };
        for (auto& c : graph.getConnections()) {
            juce::AudioProcessorGraph::NodeID farNode;
            if (isInput && c.destination.nodeID == nodeId && isTargetChannelPrescan(c.destination.channelIndex))
                farNode = c.source.nodeID;
            else if (!isInput && c.source.nodeID == nodeId && isTargetChannelPrescan(c.source.channelIndex))
                farNode = c.destination.nodeID;
            else
                continue;
            if (nodeIsMacroPort(farNode)) {
                touchesMacroPort = true;
                break;
            }
        }
    }

    // Same connection-removal logic either way — the macro-port branch below just also collects
    // which nodes were touched, for the auto-delete scan run afterwards.
    std::vector<juce::AudioProcessorGraph::NodeID> touchedNodes;
    auto doDisconnect = [this, &graph, nodeId, targetChannels, isInput, &touchedNodes] {
        std::vector<juce::AudioProcessorGraph::Connection> toRemove;
        auto isTargetChannel = [&targetChannels](int channel) {
            return std::find(targetChannels.begin(), targetChannels.end(), channel) != targetChannels.end();
        };

        for (auto& c : graph.getConnections()) {
            if (isInput) {
                if (c.destination.nodeID == nodeId && isTargetChannel(c.destination.channelIndex)) {
                    if (auto* srcNode = graph.getNodeForId(c.source.nodeID)) {
                        if (dynamic_cast<AttenuverterModule*>(srcNode->getProcessor()) != nullptr)
                            audioEngine.removeModRouting(srcNode->nodeID);
                        else {
                            toRemove.push_back(c);
                            touchedNodes.push_back(c.source.nodeID);
                        }
                    }
                }
            } else {
                if (c.source.nodeID == nodeId && isTargetChannel(c.source.channelIndex)) {
                    if (auto* dstNode = graph.getNodeForId(c.destination.nodeID)) {
                        if (dynamic_cast<AttenuverterModule*>(dstNode->getProcessor()) != nullptr)
                            audioEngine.removeModRouting(dstNode->nodeID);
                        else {
                            toRemove.push_back(c);
                            touchedNodes.push_back(c.destination.nodeID);
                        }
                    }
                }
            }
        }
        for (auto& c : toRemove)
            graph.removeConnection(c);
    };

    if (touchesMacroPort) {
        auto doDisconnectAndPrune = [this, doDisconnect, nodeId, &touchedNodes] {
            doDisconnect();
            touchedNodes.push_back(nodeId);
            for (auto touched : touchedNodes)
                autoDeleteOrphanedMacroPort(touched);
            updateComponents();
        };
        if (undoManager)
            undoManager->recordGraphAndMacroChange(graph, macros, doDisconnectAndPrune);
        else
            doDisconnectAndPrune();
    } else if (undoManager) {
        undoManager->recordStructuralChange(graph, doDisconnect);
    } else {
        doDisconnect();
    }
    repaint();
}

bool GraphEditor::isPortConnected(ModuleComponent* module, int portIndex, bool isInput, bool isMidi) const {
    if (module == nullptr)
        return false;

    juce::AudioProcessorGraph::NodeID nodeId;
    for (auto* n : audioEngine.getGraph().getNodes()) {
        if (n->getProcessor() == module->getModule()) {
            nodeId = n->nodeID;
            break;
        }
    }
    if (nodeId.uid == 0)
        return false;

    return isInput ? !smartConnections_.isInputJackFree(nodeId, portIndex, isMidi)
                   : !smartConnections_.isOutputJackFree(nodeId, portIndex, isMidi);
}

// `previousInputMap`/`previousOutputMap` are the only way to tell which visible jack each existing
// raw connection was anchored to, since the live mapping already reflects the new state.
void GraphEditor::rewireForPolyChange(ModuleComponent* module, const std::vector<LogicalPort>& previousInputMap,
                                      const std::vector<LogicalPort>& previousOutputMap) {
    if (module == nullptr)
        return;

    auto* toggled = dynamic_cast<ModuleBase*>(module->getModule());
    if (toggled == nullptr)
        return;

    auto& graph = audioEngine.getGraph();
    juce::AudioProcessorGraph::NodeID nodeId;
    for (auto* n : graph.getNodes()) {
        if (n->getProcessor() == toggled) {
            nodeId = n->nodeID;
            break;
        }
    }
    if (nodeId.uid == 0)
        return;

    // Which visible jack a raw channel belonged to *before* the toggle. The live mapping already
    // reflects the new poly state, so only the captured maps can answer this.
    auto previousJack = [](const std::vector<LogicalPort>& map, int rawChannel) {
        return (rawChannel >= 0 && rawChannel < static_cast<int>(map.size()))
                   ? map[static_cast<size_t>(rawChannel)].visibleJackIndex
                   : rawChannel;
    };

    // One user-visible cable touching the toggled module, described in jack terms so it survives the
    // raw-channel reshuffle. An N-voice fan collapses into a single Wire.
    struct Wire {
        juce::AudioProcessorGraph::NodeID farNodeId;
        int farVisibleJack = 0;
        int ownVisibleJack = 0;
        bool ownIsSource = false;
        juce::AudioProcessorGraph::NodeID attenuverterNodeId; // invalid when the cable is direct
        float attenuverterAmount = 1.0f;
        std::vector<juce::AudioProcessorGraph::Connection> rawEdges;
    };
    std::vector<Wire> wires;

    const auto connections = graph.getConnections();

    for (const auto& c : connections) {
        const bool ownIsSource = (c.source.nodeID == nodeId);
        const bool ownIsDest = (c.destination.nodeID == nodeId);
        if (ownIsSource == ownIsDest)
            continue; // not ours, or a self-loop we leave alone

        const int ownRaw = ownIsSource ? c.source.channelIndex : c.destination.channelIndex;
        const int farRaw = ownIsSource ? c.destination.channelIndex : c.source.channelIndex;
        if (ownRaw == juce::AudioProcessorGraph::midiChannelIndex ||
            farRaw == juce::AudioProcessorGraph::midiChannelIndex)
            continue; // MIDI wires do not move when a module changes its voice layout

        auto farNodeId = ownIsSource ? c.destination.nodeID : c.source.nodeID;
        int farChannel = farRaw;
        juce::AudioProcessorGraph::NodeID attenuverterNodeId;
        float attenuverterAmount = 1.0f;

        // An attenuverter is an implementation detail of one mod cable — step over it to the module
        // the user actually patched, and remember its amount so a rebuild can restore it.
        if (auto* attenNode = graph.getNodeForId(farNodeId)) {
            if (dynamic_cast<AttenuverterModule*>(attenNode->getProcessor()) != nullptr) {
                attenuverterNodeId = farNodeId;
                const auto& attenParams = attenNode->getProcessor()->getParameters();
                if (attenParams.size() > 1)
                    if (auto* amount = dynamic_cast<juce::AudioParameterFloat*>(attenParams[1]))
                        attenuverterAmount = amount->get();

                bool resolved = false;
                for (const auto& leg : connections) {
                    if (ownIsSource && leg.source.nodeID == attenuverterNodeId) {
                        farNodeId = leg.destination.nodeID;
                        farChannel = leg.destination.channelIndex;
                        resolved = true;
                        break;
                    }
                    if (!ownIsSource && leg.destination.nodeID == attenuverterNodeId &&
                        leg.destination.channelIndex == 0) {
                        farNodeId = leg.source.nodeID;
                        farChannel = leg.source.channelIndex;
                        resolved = true;
                        break;
                    }
                }
                if (!resolved)
                    continue; // half-patched attenuverter — leave it to the mod matrix
            }
        }

        auto* farNode = graph.getNodeForId(farNodeId);
        if (farNode == nullptr)
            continue;
        auto* farModule = dynamic_cast<ModuleBase*>(farNode->getProcessor());

        const int ownVisibleJack = previousJack(ownIsSource ? previousOutputMap : previousInputMap, ownRaw);
        int farVisibleJack = farChannel;
        if (farModule != nullptr)
            farVisibleJack = ownIsSource ? farModule->mapInputChannel(farChannel).visibleJackIndex
                                         : farModule->mapOutputChannel(farChannel).visibleJackIndex;

        auto existing = std::find_if(wires.begin(), wires.end(), [&](const Wire& w) {
            return w.farNodeId == farNodeId && w.farVisibleJack == farVisibleJack &&
                   w.ownVisibleJack == ownVisibleJack && w.ownIsSource == ownIsSource &&
                   w.attenuverterNodeId == attenuverterNodeId;
        });

        if (existing == wires.end()) {
            Wire w;
            w.farNodeId = farNodeId;
            w.farVisibleJack = farVisibleJack;
            w.ownVisibleJack = ownVisibleJack;
            w.ownIsSource = ownIsSource;
            w.attenuverterNodeId = attenuverterNodeId;
            w.attenuverterAmount = attenuverterAmount;
            w.rawEdges.push_back(c);
            wires.push_back(std::move(w));
        } else {
            existing->rawEdges.push_back(c); // another voice of a fan we have already seen
        }
    }

    if (wires.empty())
        return;

    // Tear every affected cable down first, so a fan that moves onto channels another cable used to
    // occupy cannot collide with the version of itself we are about to rebuild.
    for (const auto& w : wires) {
        if (w.attenuverterNodeId.uid != 0)
            audioEngine.removeModRouting(w.attenuverterNodeId);
        else
            for (const auto& c : w.rawEdges)
                graph.removeConnection(c);
    }

    for (const auto& w : wires) {
        auto* farNode = graph.getNodeForId(w.farNodeId);
        if (farNode == nullptr)
            continue;
        auto* farModule = dynamic_cast<ModuleBase*>(farNode->getProcessor());

        const ModuleBase* sourceModule = w.ownIsSource ? toggled : farModule;
        const ModuleBase* destModule = w.ownIsSource ? farModule : toggled;
        const int sourceJack = w.ownIsSource ? w.ownVisibleJack : w.farVisibleJack;
        const int destJack = w.ownIsSource ? w.farVisibleJack : w.ownVisibleJack;
        const auto sourceId = w.ownIsSource ? nodeId : w.farNodeId;
        const auto destId = w.ownIsSource ? w.farNodeId : nodeId;

        const auto link = resolvePolyLink(sourceModule, sourceJack, destModule, destJack);

        // Only a single mono cable may sit behind an attenuverter; a fan is always direct, and a
        // structural pitch/gate source is never wrapped (see carriesStructuralSignal).
        const bool useAttenuverter = link.voiceCount == 1 && destModule != nullptr &&
                                     !carriesStructuralSignal(sourceModule, link.sourceRawChannel) &&
                                     destModule->isAutoPromotableModTarget(link.destRawChannel);

        if (useAttenuverter) {
            const auto newAttenId =
                audioEngine.addModRouting(sourceId, link.sourceRawChannel, destId, link.destRawChannel);
            if (newAttenId.uid != 0 && w.attenuverterNodeId.uid != 0) {
                // Carry the old amount across, so toggling poly does not silently reset the knob.
                if (auto* newAttenNode = graph.getNodeForId(newAttenId)) {
                    const auto& attenParams = newAttenNode->getProcessor()->getParameters();
                    if (attenParams.size() > 1)
                        if (auto* amount = dynamic_cast<juce::AudioParameterFloat*>(attenParams[1]))
                            amount->setValueNotifyingHost(amount->convertTo0to1(w.attenuverterAmount));
                }
            }
        } else {
            for (int v = 0; v < link.voiceCount; ++v)
                graph.addConnection(
                    {{sourceId, link.sourceRawChannel + v * link.sourceStride}, {destId, link.destRawChannel + v}});
        }
    }

    repaint();
}

void GraphEditor::timerCallback() {
    // Compute the routing traversal once; derive display info from the same snapshot.
    cachedModRoutings = audioEngine.getModulationRoutings();
    cachedModDisplayInfo = audioEngine.getModulationDisplayInfo(cachedModRoutings);
    content.connectionAnimPhase += 0.02f;
    if (content.connectionAnimPhase >= 1.0f)
        content.connectionAnimPhase -= 1.0f;
    repaintCanvas();

    // Pressing or RELEASING Ctrl is not a mouse move, and suggestions were only recomputed from
    // updateDragPreview — so a drag that stopped moving kept showing a stale insert preview after
    // the modifier was let go (and never picked one up if Ctrl went down while the mouse was
    // still). Re-evaluate on this existing 30 Hz tick rather than a new timer, and only when the
    // sampled state actually flipped: a drag that holds its modifier costs one bool compare, and
    // refreshSmartSuggestions repaints only when the suggestion set really changed.
    refreshSuggestionsIfInsertModifierChanged();

    // Minimap (issue #159): only build the model while visible, and only when it's needed —
    // setModel() itself only repaints when the model actually changed (no repaint storm on a
    // static patch).
    if (minimap.isVisible())
        minimap.setModel(buildMinimapModel());

    // Drain the audio thread's UI reflection ring on the same 30 Hz cadence as everything else in
    // this callback — no separate free-running timer. A drain against an empty ring is just
    // one prepareToRead() call, so this is effectively free on every tick that has nothing queued.
    // Reflection never calls anything that repaints on its own: setValue(..., dontSendNotification)
    // marks the slider dirty and it rides the existing buffered-image repaint, same as any other
    // control change.
    audioEngine.getAutomationUiFeed().drain([this](const synth::AutomationUiEvent& event) {
        for (auto* comp : content.getModules()) {
            if (comp->getNodeId().uid != event.nodeId)
                continue;
            comp->reflectParameterValue(event.param, event.newNormalized);
            return;
        }
        // No live component for this NodeID (module hidden mid-teardown or already deleted) —
        // the event is simply discarded.
    });
}

// ============================================================================
// Drag-preview API
// ============================================================================
