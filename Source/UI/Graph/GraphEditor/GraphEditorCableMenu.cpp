#include "GraphEditorCableMenu.h"

#include "AI/AIStateMapper/GraphRebuildBatch.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/ModCurve.h"
#include "UI/Graph/CableColour.h"
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/ModuleViews/ModCurve/ModCurvePopover.h"

namespace synth::ui {

std::vector<CableMenuItem> cableMenuItemsFor(const graph_editor_types::VisibleCable& cable) {
    std::vector<CableMenuItem> items{CableMenuItem::Disconnect};
    const bool directMono = cable.kind == graph_editor_types::VisibleCable::Kind::ModRouting &&
                            cable.signal == CableSignal::ModCV && !cable.isPolyBus;
    if (cable.kind == graph_editor_types::VisibleCable::Kind::AttenuverterChain || directMono)
        items.push_back(CableMenuItem::EditCurve);
    return items;
}

// A direct cable drawn across macro ports has no single edge to splice, so it does not offer an item that would do
// nothing.
static bool cableCurveReachable(GraphEditor& editor, const graph_editor_types::VisibleCable& cable) {
    if (cable.kind == graph_editor_types::VisibleCable::Kind::AttenuverterChain)
        return true;
    return editor.getAudioEngine().getGraph().isConnected(
        {{juce::AudioProcessorGraph::NodeID{cable.id.srcUid}, cable.id.srcPort},
         {juce::AudioProcessorGraph::NodeID{cable.id.dstUid}, cable.id.dstPort}});
}

juce::AudioProcessorGraph::NodeID spliceAttenuverterIntoCable(GraphEditor& editor,
                                                              const graph_editor_types::VisibleCable& cable) {
    auto& graph = editor.getAudioEngine().getGraph();
    const juce::AudioProcessorGraph::NodeID src{cable.id.srcUid};
    const juce::AudioProcessorGraph::NodeID dst{cable.id.dstUid};
    const juce::AudioProcessorGraph::Connection direct{{src, cable.id.srcPort}, {dst, cable.id.dstPort}};
    if (!graph.isConnected(direct))
        return {};

    juce::AudioProcessorGraph::NodeID created;
    const auto splice = [&] {
        const synth::GraphRebuildScope rebuildScope{graph};
        using Update = juce::AudioProcessorGraph::UpdateKind;
        graph.removeConnection(direct, Update::none);
        auto node = graph.addNode(std::make_unique<AttenuverterModule>(), std::nullopt, Update::none);
        if (node == nullptr)
            return;
        if (auto* amount = dynamic_cast<juce::AudioParameterFloat*>(node->getProcessor()->getParameters()[1]))
            amount->setValueNotifyingHost(amount->convertTo0to1(1.0f));
        graph.addConnection({{src, cable.id.srcPort}, {node->nodeID, 0}}, Update::none);
        graph.addConnection({{node->nodeID, 0}, {dst, cable.id.dstPort}}, Update::none);
        created = node->nodeID;
    };
    if (auto* undo = editor.getUndoManager())
        undo->recordStructuralChange(graph, splice);
    else
        splice();
    editor.notifyModuleContentChanged();
    return created;
}

void editCableCurve(GraphEditor& editor, const graph_editor_types::VisibleCable& cable, juce::Point<int> screenPoint) {
    juce::AudioProcessorGraph::NodeID atten{cable.id.attenUid};
    if (cable.kind != graph_editor_types::VisibleCable::Kind::AttenuverterChain)
        atten = spliceAttenuverterIntoCable(editor, cable);
    if (atten.uid == 0)
        return;
    openModCurvePopover(editor, atten, editor, juce::Rectangle<int>(screenPoint.x, screenPoint.y, 1, 1), true);
}

void showCableMenu(GraphEditor& editor, const graph_editor_types::VisibleCable& cable) {
    const juce::Component::SafePointer<GraphEditor> safe(&editor);
    const auto pointer = juce::Desktop::getMousePosition();
    juce::PopupMenu menu;
    menu.addSectionHeader(juce::String(cableSignalLabel(cable.signal)) + " cable");
    for (const auto item : cableMenuItemsFor(cable)) {
        if (item == CableMenuItem::Disconnect)
            menu.addItem("Disconnect Cable", [safe, cable] {
                if (safe != nullptr)
                    safe->disconnectCable(cable);
            });
        else if (cableCurveReachable(editor, cable))
            menu.addItem("Edit curve...", [safe, cable, pointer] {
                if (safe != nullptr)
                    editCableCurve(*safe, cable, pointer);
            });
    }
    menu.showMenuAsync(contextMenuOptionsAtPointer());
}

} // namespace synth::ui
