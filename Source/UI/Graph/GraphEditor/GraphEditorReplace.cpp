// GraphEditorReplace.cpp
//
// "Replace with...": swaps one module for another type, or for a hosted plugin, keeping its
// position, compatible cables, modulation routings and MIDI Remote mappings in one undo step. GraphEditor is
// declared in GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest of the class.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"
#include "GraphEditorInternal.h"
#include "Modules/AttenuverterModule.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

void GraphEditor::replaceModule(ModuleComponent* moduleComp, const juce::String& newModuleType,
                                const std::function<void(juce::AudioProcessor&)>& configure) {
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

    auto doReplace = [this, &graph, oldNodeId, newModuleTypeCopy, configure] {
        auto* oldNode = graph.getNodeForId(oldNodeId);
        if (!oldNode)
            return;

        // 1. Create the new module
        auto newProcessor = synth::AIStateMapper::createModule(newModuleTypeCopy);
        if (!newProcessor)
            return;
        // Runs before the node joins the graph, so what it sets (a hosted plugin's identity) is inside the undo
        // snapshot, the same as GraphEditor::addModuleAtCanvasPosition's `configure`.
        if (configure)
            configure(*newProcessor);

        // 2. Snapshot old module's properties
        int posX = oldNode->properties.getWithDefault("x", 0);
        int posY = oldNode->properties.getWithDefault("y", 0);
        // Captured before removeNode() frees this node, so onModuleReplaced below can
        // still tell MidiLearnController which uuid its assignments used to target. Empty is a
        // normal case (a node MIDI Remote never touched) -- retargetNode() below is a no-op then.
        const juce::String oldNodeUuid = oldNode->properties["uuid"].toString();

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
        fireBeforeDetachAllModuleComponents();
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

        // Re-target the old node's MIDI Remote assignments onto the new one, INSIDE this mutation
        // -- so whichever undo recorder wraps doReplace (below) sees the doc's own before/after
        // JSON bracket this call exactly like it brackets the graph edit above, and one Cmd+Z
        // reverts both together (see docs/control/midi-remote.md#replace-and-duplicate).
        if (onModuleReplaced)
            onModuleReplaced(oldNodeUuid, newNodeId);
    };

    if (undoManager)
        recordReplaceModuleUndo(graph, doReplace);
    else
        doReplace();
    repaint();
}

// A plain recordStructuralChange here would leave onModuleReplaced's MIDI Remote doc edit
// either unrecorded or, if MidiLearnController pushed its own undo action, a second undo step the
// user would have to Cmd+Z separately -- so this folds the doc into the SAME transaction as the
// graph replace whenever MainComponent has wired one up (setMidiRemoteProjectDocForUndo). Headless
// callers/tests that never call it keep the original graph-only behaviour.
void GraphEditor::recordReplaceModuleUndo(juce::AudioProcessorGraph& graph, const std::function<void()>& doReplace) {
    if (midiRemoteDocForUndo_ != nullptr)
        undoManager->recordGraphAndMidiRemoteChange(graph, *midiRemoteDocForUndo_, doReplace, onMidiRemoteDocRestored);
    else
        undoManager->recordStructuralChange(graph, doReplace);
}
