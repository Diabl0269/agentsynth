// InsertGapKeyboard.cpp -- the keyboard insert-after-the-selected-card; see InsertGapKeyboard.h.

#include "InsertGapKeyboard.h"

#include "InsertGap.h"
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

} // namespace synth
