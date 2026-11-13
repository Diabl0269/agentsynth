// MacroGroupControllerDuplicateGlide.cpp
//
// A duplicated track's copy on the canvas (docs/layout/duplicate-motion.md): its cards and its macro come out of the
// original's place. MacroGroupController is declared in MacroGroupController.h.

#include "MacroGroupController.h"

#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"

#include <algorithm>

std::vector<std::pair<juce::String, juce::String>> MacroGroupController::glideCopiesOutOfOriginals(
    const std::vector<std::pair<juce::AudioProcessorGraph::NodeID, juce::AudioProcessorGraph::NodeID>>& nodes) {
    auto& glide = host_.cardGlide();
    std::vector<std::pair<juce::String, juce::String>> macroPairs;
    for (const auto& [original, copy] : nodes) {
        auto* from = host_.moduleComponentFor(original);
        auto* to = host_.moduleComponentFor(copy);
        if (from != nullptr && to != nullptr) {
            glide.noteGlideOutOf(to, copy.uid, {from->getX(), from->getY(), to->getWidth(), to->getHeight()});
            glide.noteOrigin(copy.uid, original.uid);
        }
        const auto* fromMacro = macroForNode(original);
        const auto* toMacro = macroForNode(copy);
        if (fromMacro != nullptr && toMacro != nullptr && fromMacro->id != toMacro->id &&
            std::none_of(macroPairs.begin(), macroPairs.end(),
                         [&toMacro](const auto& p) { return p.second == toMacro->id; }))
            macroPairs.emplace_back(fromMacro->id, toMacro->id);
    }
    for (const auto& [fromId, toId] : macroPairs) {
        MacroCardComponent* from = nullptr;
        MacroCardComponent* to = nullptr;
        for (auto* card : host_.macroCards()) {
            if (card->getMacroId() == fromId)
                from = card;
            else if (card->getMacroId() == toId)
                to = card;
        }
        const auto fromKey = CardGlideAnimator::macroKey(fromId);
        const auto toKey = CardGlideAnimator::macroKey(toId);
        if (from != nullptr && to != nullptr)
            glide.noteGlideOutOf(to, toKey, {from->getX(), from->getY(), to->getWidth(), to->getHeight()});
        glide.noteOrigin(toKey, fromKey);
        glide.noteOrigin(CardGlideAnimator::borderKey(toKey), CardGlideAnimator::borderKey(fromKey));
    }
    return macroPairs;
}
