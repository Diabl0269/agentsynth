// ModuleComponentLayoutEditor.cpp -- opening the card layout editor ("Edit Layout...", on the card
// itself) and the layout list ("Layout List...", beside it). docs/layout/module-card-layout.md#editing-a-layout.
#include "ModuleComponent.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/CardLayoutEditor/CardLayoutEditorComponent.h"
#include "UI/Graph/CardLayoutEditor/OnCard/CardLayoutOnCardEditor.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardEditorOwner.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace {
std::function<void(std::unique_ptr<juce::Component>)>& launcherForTest() {
    static std::function<void(std::unique_ptr<juce::Component>)> launcher;
    return launcher;
}
} // namespace

void ModuleComponent::setCardLayoutEditorLauncherForTest(
    std::function<void(std::unique_ptr<juce::Component>)> launcher) {
    launcherForTest() = std::move(launcher);
}

// "Edit Layout...": the card becomes the editor. The editor reaches the card's node by id through the
// GraphEditor, never through this card: its first write rebuilds the card, destroying this component
// while the session goes on. A hosted plugin card has its picker instead. A test takes the editor
// through the launcher seam and owns it; otherwise the GraphEditor does.
void ModuleComponent::showCardLayoutEditor() {
    if (cardBody_ == nullptr || !cardBody_->drawsFromLayout()) {
        if (onChooseKnobsRequested)
            onChooseKnobsRequested();
        return;
    }
    // Ending a running session can rebuild this card, so nothing below reads a member after it.
    auto& graph = owner;
    auto* undo = undoManager;
    const auto id = nodeId;
    const auto* shortcuts = graph.getCardKeyboard().getShortcutManager();
    auto& launcher = launcherForTest();
    if (!launcher)
        synth::ui::closeOnCardLayoutEditor(graph);
    auto editor = synth::ui::CardLayoutOnCardEditor::open(graph, undo, id, shortcuts);
    if (editor == nullptr)
        return;
    if (launcher)
        return launcher(std::move(editor));
    synth::ui::adoptOnCardLayoutEditor(graph, std::move(editor));
}

// "Layout List...": the list editor in a call-out beside the card.
void ModuleComponent::showCardLayoutList() {
    if (cardBody_ == nullptr || !cardBody_->drawsFromLayout())
        return;
    auto source = std::make_unique<synth::ui::BuiltInCardLayoutSource>(owner, undoManager, nodeId);
    auto editor = std::make_unique<synth::ui::CardLayoutEditorComponent>(std::move(source),
                                                                         owner.getCardKeyboard().getShortcutManager());
    launchCardLayoutEditorCallOutBox(std::move(editor), getScreenBounds());
}

// Default: a real juce::CallOutBox, which owns the editor and deletes it (ending its session) when it
// is dismissed. A headless test takes the editor instead, by overriding this or through the launcher
// seam (cards the canvas builds are not a test's subclass).
void ModuleComponent::launchCardLayoutEditorCallOutBox(std::unique_ptr<juce::Component> editor,
                                                       juce::Rectangle<int> anchor) {
    if (auto& launcher = launcherForTest())
        return launcher(std::move(editor));
    juce::CallOutBox::launchAsynchronously(std::move(editor), anchor, nullptr);
}
