// PortConnector.cpp -- connecting two jacks with the cable drop's rules, and the panel's picks built on it (a jack, a
// knob). The new module is in PortConnectorNewModule.cpp. docs/layout/cables.md#port-connections-panel.

#include "PortConnector.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModDot/ModDotController.h"

namespace synth::ui {

using NodeID = juce::AudioProcessorGraph::NodeID;

// The rules of a completed cable drag, moved here whole from GraphEditor::endConnectionDrag so a panel pick and a drop
// can never disagree. Only the direct-jack case lives here; a drop on a collapsed macro's card stays with the drag.
PortConnector::Result PortConnector::connectJacks(GraphEditor& editor, NodeID srcId, int srcJack, NodeID dstId,
                                                  int dstJack, bool isMidi, bool recordUndo,
                                                  std::optional<juce::Point<float>> slideFrom) {
    auto& graph = editor.audioEngine.getGraph();
    if (graph.getNodeForId(srcId) == nullptr || graph.getNodeForId(dstId) == nullptr)
        return {};
    Result result;
    result.connected = true;

    // A MIDI cable from a Track In node landing here may newly make some audio reach the output with no channel --
    // build one, in the SAME undo step as the connection itself (and as any auto-created macro port the connection
    // also mints, so a Track In dragged across a macro boundary straight onto an unchanneled instrument gets ALL of
    // it undone by one Cmd+Z). Gated by autoCreateChannelOnConnectEnabled (Preferences); OFF (or not a MIDI cable from
    // a Track In) falls through to the plain connect below
    // (see docs/mixer/mixer.md#channels-follow-audio-not-tracks "main workflow").
    if (editor.autoCreateChannelOnConnectEnabled && isMidi && editor.nodeIsTimelineMidiSource(srcId)) {
        auto doMutation = [&editor, srcId, srcJack, dstId, dstJack] {
            if (!editor.autoCreateMacroPortsOnDragEnabled ||
                !editor.macroController_.maybeAutoCreateMacroPortsForDrag(srcId, srcJack, dstId, dstJack,
                                                                          /*isMidi=*/true, /*recordUndo=*/false))
                editor.connectPorts(srcId, srcJack, dstId, dstJack, /*isMidi=*/true, /*recordUndo=*/false);
            // dstId is always the real destination node, whether or not either side just got a minted macro port
            // above -- maybeAutoCreateMacroPortsForDrag wires any port it mints straight through to this same node --
            // so the search for un-channeled output feeds always starts here.
            editor.maybeAutoCreateChannelAfterConnect(dstId);
            editor.updateComponents();
        };
        if (recordUndo && editor.undoManager)
            editor.undoManager->recordGraphAndMacroChange(graph, editor.macros, doMutation);
        else
            doMutation();
        return result;
    }

    // If this connection crosses a macro boundary (an EXPANDED macro's member on one side, something outside that
    // same macro on the other), mint and wire a matching port instead of the plain direct connection. Gated by
    // autoCreateMacroPortsOnDragEnabled (Preferences); when it handles the connection the plain connectPorts is
    // skipped entirely and the cables it made slide in from the drop point
    // (see docs/macros/auto-ports.md#ports-on-a-cable-drag).
    std::vector<GraphEditor::VisibleCable> cablesBeforeDrop;
    if (editor.autoCreateMacroPortsOnDragEnabled)
        cablesBeforeDrop = editor.rebuildVisibleCables();
    if (editor.autoCreateMacroPortsOnDragEnabled &&
        editor.macroController_.maybeAutoCreateMacroPortsForDrag(srcId, srcJack, dstId, dstJack, isMidi, recordUndo)) {
        result.mintedMacroPorts = true;
        if (slideFrom.has_value())
            editor.armMacroPortSlide(cablesBeforeDrop, *slideFrom);
    } else {
        editor.connectPorts(srcId, srcJack, dstId, dstJack, isMidi, recordUndo);
    }
    return result;
}

PortConnector::Result PortConnector::connectToJack(GraphEditor& editor, const PortRef& jack, const PortRef& other) {
    if (jack.isInput == other.isInput || jack.isMidi != other.isMidi)
        return {};
    const auto& src = jack.isInput ? other : jack;
    const auto& dst = jack.isInput ? jack : other;
    const auto before = editor.snapshotCablesForRetract();
    const auto result = connectJacks(editor, src.node, src.jack, dst.node, dst.jack, jack.isMidi, /*recordUndo=*/true);
    editor.repaintCanvas(); // drops the cable memo, or the grow below would diff against the cables as they were
    editor.retractCablesGoneSince(before, /*growAdded=*/true);
    return result;
}

int PortConnector::rawSourceChannel(GraphEditor& editor, NodeID node, int jack) {
    auto* graphNode = editor.audioEngine.getGraph().getNodeForId(node);
    auto* module = graphNode != nullptr ? dynamic_cast<ModuleBase*>(graphNode->getProcessor()) : nullptr;
    if (module == nullptr)
        return -1;
    const auto targets = module->getJackTargets(jack, /*isInput=*/false);
    for (const auto& t : targets)
        if (t.role == PortRole::ModCV)
            return t.rawHeadChannel;
    return targets.front().rawHeadChannel;
}

bool PortConnector::connectToKnob(GraphEditor& editor, const PortRef& jack, NodeID destNode, int destChannel) {
    if (jack.isInput || jack.isMidi)
        return false;
    const int raw = rawSourceChannel(editor, jack.node, jack.jack);
    if (raw < 0)
        return false;
    const auto before = editor.snapshotCablesForRetract();
    const auto attenuverter =
        editor.connectModulationSource(jack.node, raw, destNode, destChannel, kModDotNewSourceDepth);
    editor.repaintCanvas();
    editor.retractCablesGoneSince(before, /*growAdded=*/true);
    return attenuverter.uid != 0;
}

} // namespace synth::ui
