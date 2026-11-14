// ModuleComponentLayoutEditor.cpp -- opening the card layout editor ("Edit Layout...", on the card
// itself) and the call-out a hosted plugin's list editor opens in. docs/layout/module-card-layout.md#editing-a-layout.
#include "ModuleComponent.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardLayoutQuickEdit.h"
#include "UI/Graph/CardLayoutEditor/OnCard/CardLayoutOnCardEditor.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardEditorOwner.h"
#include "UI/Graph/CardWidgets/CardButton.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/PopupHost.h"

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

// Default: a real juce::CallOutBox, which owns the editor and deletes it (ending its session) when it
// is dismissed. A headless test takes the editor instead, by overriding this or through the launcher
// seam (cards the canvas builds are not a test's subclass).
void ModuleComponent::launchCardLayoutEditorCallOutBox(std::unique_ptr<juce::Component> editor,
                                                       juce::Rectangle<int> anchor) {
    if (auto& launcher = launcherForTest())
        return launcher(std::move(editor));
    synth::ui::launchPopover(std::move(editor), anchor, nullptr);
}

// Right-click on a Macros card's XY pad or button. A button offers Automate and MIDI Learn, as its knob did; an XY pad
// drives two macros, so each gets its own submenu with Automate and the MIDI Learn block. False for any other control.
bool ModuleComponent::showMacroWidgetMenu(juce::Component& pad) {
    if (dynamic_cast<synth::ui::CardButton*>(&pad) != nullptr) {
        if (const auto* entry = midiLearnableRegistry_.find(&pad); entry != nullptr)
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(entry->param))
                showAutomateMenuForSlider(ranged);
        return true;
    }
    if (pad.getComponentID() != "XY pad")
        return false;
    if (cardBody_ == nullptr)
        return true;
    const auto params = cardBody_->xyPadParams(pad);
    if (!params.has_value())
        return true;
    auto* const xParam = params->first;
    auto* const yParam = params->second;
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    const auto nodeIdCopy = nodeId;

    juce::PopupMenu menu;
    for (auto* param : {xParam, yParam}) {
        if (param == nullptr)
            continue;
        const juce::String paramId = param->paramID;
        juce::PopupMenu sub;
        sub.addItem("Automate '" + param->getName(100) + "'", [safeThis, nodeIdCopy, paramId] {
            if (safeThis != nullptr && safeThis->owner.onAutomateParameterRequested)
                safeThis->owner.onAutomateParameterRequested(nodeIdCopy, paramId);
        });
        appendMidiLearnMenuItems(sub, param);
        menu.addSubMenu(param->getName(100), sub);
    }
    if (xParam != nullptr)
        synth::appendCardLayoutMenuItems(menu, owner, undoManager, nodeId, cardBody_.get(), xParam->paramID);
    if (menu.getNumItems() > 0)
        showContextMenuHook_(menu);
    return true;
}
