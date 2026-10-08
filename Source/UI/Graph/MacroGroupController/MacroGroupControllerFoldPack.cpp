// MacroGroupControllerFoldPack.cpp
//
// "Fold and Pack Macros" (docs/macros/menu-and-membership.md): every selected macro folds, and with the preference on
// the folded cards are tidied into a grid (MacroPackLayout.h). Fold and pack are ONE undo step. The model is final in
// one go: the packing moves go through moveUnitBy, the mover makeRoomFor uses, and the folds fly straight to the packed
// card positions, so a card never jumps; a selected card that was already folded glides there in the same glide
// Scope. With the "tidy" preference the existing Auto Arrange then closes up the space the macros left.
// MacroGroupController is declared in MacroGroupController.h.

#include "MacroGroupController.h"
#include "MacroGroupControllerInternal.h"
#include "MacroPackLayout.h"

#include "AppUndoManager.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/SelectionModel.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"

std::set<juce::String> MacroGroupController::selectionFoldTargets(bool& anyExpanded) const {
    std::set<juce::String> targets;
    anyExpanded = false;
    for (auto id : host_.getSelection().getSelected()) {
        const auto uuid = nodeUuidFor(id);
        if (uuid.isEmpty())
            continue;
        if (const auto* m = macro_toggle::targetFor(host_.getMacros(), uuid)) {
            targets.insert(m->id);
            anyExpanded = anyExpanded || !m->collapsed;
        }
    }
    return targets;
}

// Moves the folded top-level cards of `ids` to the grid. A nested macro keeps its place: its parent's hull holds it.
void MacroGroupController::packCollapsedCards(const std::set<juce::String>& ids) {
    std::vector<juce::String> packed;
    std::vector<juce::Rectangle<int>> rects;
    for (const auto& id : ids) {
        const auto* macro = host_.getMacros().find(id);
        if (macro == nullptr || !macro->collapsed || macro->parentId.isNotEmpty())
            continue;
        const auto* card = getMacroCard(id);
        packed.push_back(id);
        rects.push_back(card != nullptr ? card->getBounds() : macro->bounds);
    }
    const auto targets = macro_pack::packedPositions(rects);
    for (size_t i = 0; i < packed.size(); ++i)
        moveUnitBy("m:" + packed[i], targets[i] - rects[i].getPosition());
    refreshAfterMove();
}

void MacroGroupController::foldAndPackSelectionMacros() {
    bool anyExpanded = false;
    const auto targets = selectionFoldTargets(anyExpanded);
    if (targets.empty()) {
        host_.reportStatusMessage("Select a macro's modules to fold and pack macros.");
        return;
    }

    // Any open macro: fold them all (and pack); every one already folded: expand them all where their cards sit.
    const bool pack = anyExpanded && packMacrosOnCollapse_;
    const auto foldBefore = snapshotFoldState();
    CardGlideAnimator::Scope glide(host_.cardGlide()); // folded cards fly in, already-folded ones glide, together
    auto doAll = [this, targets, anyExpanded, pack] {
        batchingFolds_ = true;
        for (const auto& macroId : targets)
            applyMacroCollapsed(macroId, anyExpanded);
        batchingFolds_ = false;
        if (pack) {
            packCollapsedCards(targets);
            if (tidyCanvasOnPack_ && arrangeCanvasHook)
                arrangeCanvasHook(); // the existing Auto Arrange, in this same undo step and glide
        }
    };

    if (host_.undo())
        host_.undo()->recordGraphAndMacroChange(host_.graph(), host_.getMacros(), doAll);
    else
        doAll();

    foldChangedMacros(foldBefore);
    host_.requestRepaint();
}
