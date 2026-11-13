// PortConnectorNewModule.cpp -- "New <module>" from the port panel's search: a module beside the jack's card with its
// first compatible jack cabled to the jack, as one undo step. A sibling of GraphEditor::addModulationSourceModule built
// on the same pieces. docs/layout/cables.md#port-connections-panel.

#include "PortConnector.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"

namespace synth::ui {

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

constexpr int kBesideGap = 24; // px between the card and the module made for it

// The first visible jack of `module` that can take a cable from `from`; -1 when none.
int firstJackFor(const ModuleBase& module, const PortRef& from) {
    if (from.isMidi)
        return (from.isInput ? module.producesMidi() : module.acceptsMidi())
                   ? juce::AudioProcessorGraph::midiChannelIndex
                   : -1;
    const bool wantInput = !from.isInput;
    const int count = wantInput ? module.getVisibleInputPortCount() : module.getVisibleOutputPortCount();
    for (int i = 0; i < count; ++i) {
        // A parameter CV or a detector key is reached through a knob or by hand, never as a module's first jack.
        bool signalJack = false;
        for (const auto& t : module.getJackTargets(i, wantInput))
            signalJack = signalJack || t.role == PortRole::Audio || t.role == PortRole::Pitch ||
                         t.role == PortRole::Gate || t.role == PortRole::Other;
        if (signalJack)
            return i;
    }
    return -1;
}

// To the right of the card for a cable leaving an output (the new module is where it goes), to its left for an input;
// the other side when there is no room.
juce::Point<int> besideCard(juce::Rectangle<int> card, juce::Point<int> size, bool toTheRight) {
    if (card.isEmpty())
        return {LayoutUtil::kArrangeOriginX, LayoutUtil::kArrangeOriginY};
    const juce::Point<int> right{card.getRight() + kBesideGap, card.getY()};
    const juce::Point<int> left{card.getX() - kBesideGap - size.x, card.getY()};
    if (toTheRight)
        return right;
    return left.x >= LayoutUtil::kArrangeOriginX ? left : right;
}

} // namespace

int PortConnector::firstCompatibleJack(const juce::String& typeName, const PortRef& jack) {
    auto probe = synth::AIStateMapper::createModule(typeName);
    auto* module = dynamic_cast<ModuleBase*>(probe.get());
    return module != nullptr ? firstJackFor(*module, jack) : -1;
}

NodeID PortConnector::addModuleAndConnect(GraphEditor& editor, const PortRef& jack, const juce::String& typeName) {
    auto& graph = editor.audioEngine.getGraph();
    auto* anchor = graph.getNodeForId(jack.node);
    if (anchor == nullptr || GraphEditor::isSingletonIOModule(typeName))
        return {};
    auto processor = synth::AIStateMapper::createModule(typeName);
    if (processor == nullptr)
        return {};
    editor.applyDefaultDualIOForNewModule(*processor, typeName);
    auto* moduleBase = dynamic_cast<ModuleBase*>(processor.get());
    const int newJack = moduleBase != nullptr ? firstJackFor(*moduleBase, jack) : -1;
    if (newJack < 0)
        return {};
    const auto* macro = editor.macros.findByMember(synth::AIStateMapper::ensureNodeUuid(anchor));
    const juce::String macroId = macro != nullptr ? macro->id : juce::String();

    NodeID created;
    auto proc = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(processor));
    auto mutation = [&editor, &graph, &created, proc, jack, typeName, macroId, newJack] {
        // The hull the module is about to join is where it should land, not an obstacle (resolvePlacement).
        juce::ScopedValueSetter<juce::String> joinScope(editor.macroDragJoinId_, macroId);
        auto node = graph.addNode(std::move(*proc));
        if (node == nullptr)
            return;
        created = node->nodeID;
        const auto uuid = synth::AIStateMapper::ensureNodeUuid(node.get());

        // Two passes, like a library drop: an estimated size places the node before its card exists, then the real
        // card's size re-resolves it. Final at once (nothing was dragged), before the cable reads the positions.
        const auto estimate = GraphEditor::estimateModuleSize(typeName);
        auto* anchorCard = editor.moduleComponentFor(jack.node);
        const auto desired = besideCard(anchorCard != nullptr ? anchorCard->getBounds() : juce::Rectangle<int>{},
                                        estimate, /*toTheRight=*/!jack.isInput);
        const auto initial = editor.resolvePlacement(desired, estimate.x, estimate.y, NodeID{});
        node->properties.set("x", initial.x);
        node->properties.set("y", initial.y);
        editor.updateComponents();
        if (auto* comp = editor.moduleComponentFor(created)) {
            comp->setTopLeftPosition(
                editor.resolvePlacement(comp->getPosition(), comp->getWidth(), comp->getHeight(), created));
            editor.updateModulePosition(comp);
        }
        // Joined before the cable, so both ends are in the same macro and no port is minted for it.
        if (macroId.isNotEmpty())
            editor.macroController_.addSelectionToMacro(macroId, {uuid}, /*recordUndo=*/false);
        else
            editor.macroController_.makeRoomFor("n:" + juce::String((juce::int64)created.uid));

        const auto& src = jack.isInput ? PortRef{created, newJack, false, jack.isMidi} : jack;
        const auto& dst = jack.isInput ? jack : PortRef{created, newJack, true, jack.isMidi};
        connectJacks(editor, src.node, src.jack, dst.node, dst.jack, jack.isMidi, /*recordUndo=*/false);
        editor.reflowOutputDock();
        editor.updateComponents();
    };
    const auto before = editor.snapshotCablesForRetract();
    if (editor.undoManager != nullptr)
        editor.undoManager->recordGraphAndMacroChange(graph, editor.macros, mutation);
    else
        mutation();
    editor.repaintCanvas();
    editor.retractCablesGoneSince(before, /*growAdded=*/true);
    return created;
}

} // namespace synth::ui
