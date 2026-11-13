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

// A mixer reorder's canvas follow-up (docs/mixer/row-motion.md#the-canvas-follows-a-reorder), run inside the reorder's
// own undo record. The cards keep the slots they had (the first card's left edge, then each gap between neighbours) and
// are dealt into them in the new chain order, so two neighbours of different widths swap without overlapping or opening
// a hole. Only when every card is on screen, in one container (the root, or one macro), on one row and left to right in
// the old chain order: anywhere else the user laid the cards out by hand and they stay put. The positions land through
// the canvas's own card move (setTopLeftPosition + updateModulePosition) in one CardGlideAnimator::Scope, as Auto
// Arrange and an insert's make-room do, so the cards and their cables glide, and an undo of the record glides them
// back.
void followReorderOnCanvas(GraphEditor& editor, const std::vector<juce::AudioProcessorGraph::NodeID>& before,
                           const std::vector<juce::AudioProcessorGraph::NodeID>& after) {
    // How far apart two card tops may be and still count as one row on the canvas.
    constexpr int kSameRowTolerance = 8;
    if (before.size() < 2 || before.size() != after.size())
        return;
    CardGlideAnimator::Scope glide(editor.getCardGlide());
    editor.updateComponents(); // the cards the reorder rewired, before their bounds are read
    std::vector<ModuleComponent*> cards;
    const auto* container = editor.getMacroController().macroForNode(before.front());
    for (const auto id : before) {
        auto* card = cardFor(editor, id);
        if (card == nullptr || !card->isVisible() || editor.isOutputDockNode(id) ||
            editor.getMacroController().macroForNode(id) != container)
            return;
        if (!cards.empty() && (std::abs(card->getY() - cards.front()->getY()) > kSameRowTolerance ||
                               card->getX() <= cards.back()->getRight()))
            return;
        cards.push_back(card);
    }
    std::vector<int> gaps;
    for (size_t i = 0; i + 1 < cards.size(); ++i)
        gaps.push_back(cards[i + 1]->getX() - cards[i]->getRight());
    int x = cards.front()->getX();
    for (size_t i = 0; i < after.size(); ++i) {
        auto* card = cardFor(editor, after[i]);
        if (card == nullptr)
            return;
        card->setTopLeftPosition(x, card->getY());
        editor.updateModulePosition(card);
        x += card->getWidth() + (i < gaps.size() ? gaps[i] : 0);
    }
    editor.notifyModuleContentChanged();
}

} // namespace synth
