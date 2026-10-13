// InsertGapKeyboard.cpp -- the keyboard insert-after-the-selected-card; see InsertGapKeyboard.h.

#include "InsertGapKeyboard.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "InsertGap.h"
#include "MacroSet.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace synth {

// With a card after it in its row, the add goes through the same commit a drop between two cards uses (InsertGap's
// armAfter + Commit inside addModuleAtCanvasPosition). With none, there is nothing to push: the module goes one
// spacing to the right of the card, level with it, and the usual placement (or the macro's own make-room, inside an
// open macro) settles it.
bool insertModuleAfterSelectedCard(GraphEditor& editor, const juce::String& name) {
    const auto selected = editor.getSelectedNodes();
    if (selected.size() != 1)
        return false;
    const auto id = selected.front();
    ModuleComponent* card = nullptr;
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == id)
            card = comp;
    auto& controller = editor.getMacroController();
    if (card == nullptr || !card->isVisible() || editor.isOutputDockNode(id) || controller.nodeIsMacroPort(id))
        return false;

    const auto* owner = controller.macroForNode(id);
    const juce::String container = owner != nullptr ? owner->id : juce::String();
    const auto size = GraphEditor::estimateModuleSize(name);
    auto& gap = controller.insertGap();
    const juce::String key = "n:" + juce::String(static_cast<juce::int64>(id.uid));
    if (gap.armAfter(container, key, size)) {
        editor.addModuleAtCanvasPosition(name, gap.pendingSlot(), {}, container);
        gap.clearPending(); // the add was refused (a second Audio Output, a project still loading)
        return true;
    }
    const auto bounds = card->getBounds();
    editor.addModuleAtCanvasPosition(name, {bounds.getRight() + insert_gap::kDefaultSpacing, bounds.getY()}, {},
                                     container);
    return true;
}

namespace {
ModuleComponent* cardFor(GraphEditor& editor, juce::AudioProcessorGraph::NodeID id) {
    for (auto* comp : editor.getModuleComponents())
        if (comp != nullptr && comp->getNodeId() == id)
            return comp;
    return nullptr;
}
} // namespace

// The same commit a keyboard insert uses (InsertGap armAfter + Commit), but the node is made here rather than through
// addModuleAtCanvasPosition, so the cables `wire` adds land inside the very same undo record.
juce::AudioProcessorGraph::NodeID
insertModuleAfterNode(GraphEditor& editor, AppUndoManager& undoManager, MacroSet& macros, const juce::String& name,
                      juce::AudioProcessorGraph::NodeID anchor,
                      const std::function<bool(juce::AudioProcessorGraph::NodeID)>& wire) {
    using NodeID = juce::AudioProcessorGraph::NodeID;
    auto& graph = editor.getAudioEngine().getGraph();
    auto& controller = editor.getMacroController();
    auto* anchorCard = cardFor(editor, anchor);
    const bool onScreen = anchorCard != nullptr && anchorCard->isVisible() && !editor.isOutputDockNode(anchor) &&
                          !controller.nodeIsMacroPort(anchor);
    const auto* owner = controller.macroForNode(anchor);
    const juce::String container = onScreen && owner != nullptr ? owner->id : juce::String();
    const auto size = GraphEditor::estimateModuleSize(name);
    auto& gap = controller.insertGap();
    const bool armed =
        onScreen && gap.armAfter(container, "n:" + juce::String(static_cast<juce::int64>(anchor.uid)), size);

    juce::Point<int> start;
    if (armed) {
        start = gap.pendingSlot();
    } else if (onScreen) {
        start = {anchorCard->getRight() + insert_gap::kDefaultSpacing, anchorCard->getY()};
    } else if (auto* node = graph.getNodeForId(anchor)) {
        const auto anchorName = node->getProcessor() != nullptr ? node->getProcessor()->getName() : name;
        start = {(int)node->properties.getWithDefault("x", 0) + GraphEditor::estimateModuleSize(anchorName).x +
                     insert_gap::kDefaultSpacing,
                 (int)node->properties.getWithDefault("y", 0)};
    }

    NodeID added;
    std::optional<InsertGap::Commit> commit;
    if (armed)
        commit.emplace(gap);
    undoManager.recordGraphAndMacroChange(graph, macros, [&] {
        auto processor = AIStateMapper::createModule(name);
        if (processor == nullptr)
            return;
        auto node = graph.addNode(std::move(processor));
        if (node == nullptr)
            return;
        AIStateMapper::ensureNodeUuid(node.get());
        node->properties.set("x", start.x);
        node->properties.set("y", start.y);
        if (!wire(node->nodeID)) {
            graph.removeNode(node->nodeID);
            return;
        }
        added = node->nodeID;
        CardGlideAnimator::Scope glide(editor.getCardGlide());
        editor.updateComponents();
        if (auto* card = cardFor(editor, added); card != nullptr && armed) {
            if (const auto slot = gap.land(added, {card->getWidth(), card->getHeight()})) {
                card->setTopLeftPosition(*slot);
                editor.updateModulePosition(card);
            }
            editor.getCardGlide().noteEnter(card, added.uid);
        }
    });
    commit.reset();
    gap.clearPending();
    return added;
}

} // namespace synth
