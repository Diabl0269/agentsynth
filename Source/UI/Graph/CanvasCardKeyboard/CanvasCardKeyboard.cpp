// CanvasCardKeyboard.cpp
//
// The canvas's card keys (docs/layout/selection.md#keyboard). The canvas selection IS the
// keyboard card focus: an arrow changes the selection the mouse would, so the theme's selected
// treatment (accent border and glow) is the card's ring and there is no second "focused card".

#include "CanvasCardKeyboard.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace {

using synth::ui::CardDirection;

struct DirectionKey {
    const char* selectAction;
    const char* moveAction;
    int keyCode;
    CardDirection direction;
    juce::Point<int> step;
};

// The four directions with their default keys: bare arrows select, Alt+arrows move.
const DirectionKey kDirections[] = {
    {"canvasSelectCardLeft", "canvasMoveCardLeft", juce::KeyPress::leftKey, CardDirection::Left, {-1, 0}},
    {"canvasSelectCardRight", "canvasMoveCardRight", juce::KeyPress::rightKey, CardDirection::Right, {1, 0}},
    {"canvasSelectCardUp", "canvasMoveCardUp", juce::KeyPress::upKey, CardDirection::Up, {0, -1}},
    {"canvasSelectCardDown", "canvasMoveCardDown", juce::KeyPress::downKey, CardDirection::Down, {0, 1}},
};

ModuleComponent* findCard(GraphEditor& editor, juce::AudioProcessorGraph::NodeID id) {
    for (auto* card : editor.getModuleComponents())
        if (card != nullptr && card->getNodeId() == id)
            return card;
    return nullptr;
}

} // namespace

CanvasCardKeyboard::CanvasCardKeyboard(GraphEditor& editor, AppUndoManager* undoManager)
    : editor_(editor)
    , undo_(undoManager) {}

// Strict once a manager is installed (a cleared binding means no key), the default key without
// one -- the same contract as PianoRollComponent::matchesAction.
bool CanvasCardKeyboard::matches(const juce::KeyPress& key, const char* actionId,
                                 const juce::KeyPress& fallback) const {
    if (shortcuts_ == nullptr)
        return key == fallback;
    const auto binding = shortcuts_->getBinding(actionId);
    return binding.isValid() && ShortcutManager::keyPressMatches(binding, key);
}

// A key that bubbled up from a control inside a card (a toggle ignoring Left, a combo) or from the
// Mod Matrix must not move the canvas selection out from under it. No focused component at all is
// a headless test driving the canvas directly.
bool CanvasCardKeyboard::focusIsInsideACanvasChild() const {
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return focused != nullptr && focused != &editor_ && editor_.isParentOf(focused);
}

bool CanvasCardKeyboard::keyPressed(const juce::KeyPress& key) {
    if (focusIsInsideACanvasChild())
        return false;

    for (const auto& d : kDirections) {
        if (matches(key, d.selectAction, juce::KeyPress(d.keyCode, juce::ModifierKeys::noModifiers, 0)))
            return selectInDirection(d.direction);
        if (matches(key, d.moveAction, juce::KeyPress(d.keyCode, juce::ModifierKeys::altModifier, 0)))
            return moveSelectionBy(d.step);
    }
    if (matches(key, "canvasEnterCard", juce::KeyPress(juce::KeyPress::returnKey)))
        return enterSelectedCard();
    return false;
}

// Only visible cards count, as for selectAdjacentModule: a collapsed macro hides its members. A
// multi-selection moves from its most recently added card. With no card in that direction the
// key is still consumed and nothing changes.
bool CanvasCardKeyboard::selectInDirection(CardDirection direction) {
    std::vector<synth::ui::StepModule> cards;
    for (auto* card : editor_.getModuleComponents())
        if (card != nullptr && card->getModule() != nullptr && card->isVisible())
            cards.push_back({card->getNodeId(), card->getBounds().toFloat()});
    if (cards.empty())
        return false;

    const auto selected = editor_.getSelectedNodes();
    const auto from = selected.empty() ? juce::AudioProcessorGraph::NodeID{} : selected.back();
    const auto target = synth::ui::nearestCardInDirection(cards, from, direction);
    if (target.uid == 0)
        return true;

    editor_.selectModule(target, /*additive=*/false);
    for (const auto& c : cards)
        if (c.id == target && !editor_.getVisibleCanvasRect().contains(c.bounds))
            editor_.centreViewOn(c.bounds.getCentre());
    return true;
}

// Mirrors ModuleComponent::mouseUp's commit exactly: one card snaps and de-overlaps through
// finalizeModuleDrag inside a captureBeforeState/pushSnapshotFromCapture pair; a group moves as one
// rigid body through beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag, recorded with the
// macros so a carried nested macro moves in the same step. A destination that overlaps a
// neighbour is pushed to the nearest free slot, as a drop there would be. A selection holding a
// collapsed macro's hidden members is left alone: only the macro card's own drag moves its bounds.
bool CanvasCardKeyboard::moveSelectionBy(juce::Point<int> gridSteps) {
    const auto selected = editor_.getSelectedNodes();
    if (selected.empty())
        return false;
    for (auto id : selected)
        if (auto* c = findCard(editor_, id); c != nullptr && !c->isVisible())
            return false;

    auto& graph = editor_.getAudioEngine().getGraph();
    const auto delta = gridSteps * synth::LayoutUtil::kGridSize;

    if (undo_ != nullptr)
        undo_->captureBeforeState(graph);
    editor_.beginSelectionDrag();
    if (editor_.isSelectionDragActive()) {
        editor_.dragSelectionBy(delta, nullptr);
        auto doFinalize = [this] { editor_.finalizeSelectionDrag(); };
        if (undo_ != nullptr)
            undo_->recordGraphAndMacroChange(graph, editor_.getMacros(), doFinalize,
                                             undo_->takeCapturedGraphBeforeState());
        else
            doFinalize();
        return true;
    }
    editor_.cancelSelectionDrag();

    ModuleComponent* card = nullptr;
    for (auto id : selected)
        if (auto* c = findCard(editor_, id); c != nullptr && (card == nullptr || !editor_.isOutputDockNode(id)))
            card = c;
    if (card != nullptr) {
        card->setTopLeftPosition(card->getPosition() + delta);
        editor_.finalizeModuleDrag(card);
    }
    if (undo_ != nullptr)
        undo_->pushSnapshotFromCapture(graph);
    return card != nullptr;
}

bool CanvasCardKeyboard::enterSelectedCard() {
    const auto selected = editor_.getSelectedNodes();
    if (selected.empty())
        return false;
    auto* card = findCard(editor_, selected.back());
    return card != nullptr && card->isVisible() && card->enterFromKeyboard();
}
