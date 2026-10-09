// MacroGroupControllerDeleteReflow.cpp
//
// What the canvas does around a card delete (docs/macros/macros.md#deleting-a-card-inside-an-open-macro):
//   - the neighbours the deleted card had pushed aside while it grew come home (the card is gone, so nothing blocks
//     them any more); and
//   - inside an OPEN macro the hole closes: the cards after the deleted one move up to fill it, and the macro's border,
//     which follows its members, shrinks with them. On the open canvas the hole stays.
// Both run inside the delete's own undo record, so Cmd+Z restores every position exactly. MacroGroupController is
// declared in MacroGroupController.h.

#include "MacroGroupController.h"

#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"

#include <algorithm>
#include <limits>

namespace {
using synth::LayoutUtil::LayoutUnit;

bool overlapsVertically(const juce::Rectangle<int>& a, const juce::Rectangle<int>& b) {
    return a.getY() < b.getBottom() && b.getY() < a.getBottom();
}
bool overlapsHorizontally(const juce::Rectangle<int>& a, const juce::Rectangle<int>& b) {
    return a.getX() < b.getRight() && b.getX() < a.getRight();
}

// The largest shift (a whole number of grid steps below `wanted`) that keeps every mover clear of every unit that
// stays put and inside the canvas; 0 when not even one step fits.
int clearShift(const std::vector<const LayoutUnit*>& movers, const std::vector<LayoutUnit>& units, int wanted,
               bool horizontal) {
    auto fits = [&](int shift) {
        for (const auto* mover : movers) {
            const auto moved = horizontal ? mover->rect.translated(-shift, 0) : mover->rect.translated(0, -shift);
            if (moved.getX() < 0 || moved.getY() < 0)
                return false;
            for (const auto& other : units) {
                const bool alsoMoves = std::find(movers.begin(), movers.end(), &other) != movers.end();
                if (!alsoMoves && moved.expanded(synth::LayoutUtil::kCollisionGap).intersects(other.rect))
                    return false;
            }
        }
        return true;
    };
    for (int shift = wanted; shift > 0; shift -= synth::LayoutUtil::kGridSize)
        if (fits(shift))
            return shift;
    return 0;
}
} // namespace

// Cards that are macro ports, or sit in a collapsed macro, are no layout units, so they leave no hole to close.
MacroGroupController::DeleteReflow
MacroGroupController::captureDeleteReflow(const std::vector<juce::AudioProcessorGraph::NodeID>& ids) {
    DeleteReflow reflow;
    for (auto id : ids) {
        if (const auto found = moduleDisplaced_.find("n:" + juce::String((juce::int64)id.uid));
            found != moduleDisplaced_.end()) {
            reflow.pushed.insert(reflow.pushed.end(), found->second.begin(), found->second.end());
            moduleDisplaced_.erase(found);
        }
        const auto* owner = macroForNode(id);
        auto* card = host_.moduleComponentFor(id);
        if (owner == nullptr || card == nullptr || !card->isVisible() ||
            host_.getMacros().isEffectivelyCollapsed(owner->id))
            continue;
        if (owner->memberIsPort(nodeUuidFor(id)))
            continue;
        reflow.slots.emplace_back(owner->id, card->getBounds());
    }
    return reflow;
}

// Pushed neighbours first, so the macro closes around cards that are back where they started. A slot closes after the
// ones to its right, so a row cleared from the right-hand end inward closes cleanly.
void MacroGroupController::applyDeleteReflow(const DeleteReflow& reflow) {
    CardGlideAnimator::Scope glide(host_.cardGlide());
    bool movedAny = false;
    if (!reflow.pushed.empty())
        returnRecordedNeighbours(reflow.pushed, movedAny);
    auto slots = reflow.slots;
    std::sort(slots.begin(), slots.end(), [](const auto& a, const auto& b) {
        return a.second.getX() != b.second.getX() ? a.second.getX() > b.second.getX()
                                                  : a.second.getY() > b.second.getY();
    });
    for (const auto& [macroId, rect] : slots)
        closeGapAt(macroId, rect);
}

// "Later" follows how cards sit in a macro: they are placed freely in rows and columns. The cards of the same row (they
// overlap the hole vertically) that lie wholly to its right move left, together, so the nearest lands exactly where
// the deleted card stood and the spacing among them is kept. A card with nothing after it in its row closes the
// hole from below instead: the cards in its column (they overlap it horizontally) that lie wholly under it move up
// the same way. A move that would run into another card or past the canvas edge is shortened to whole grid steps, or
// dropped.
void MacroGroupController::closeGapAt(const juce::String& macroId, juce::Rectangle<int> slot) {
    const auto* macro = host_.getMacros().find(macroId);
    if (macro == nullptr || host_.getMacros().isEffectivelyCollapsed(macroId))
        return;
    const auto units = buildLayoutUnits(macroId);
    // A neighbour the deleted card had pushed aside may already be back in its place (it landed between them): then
    // there is no hole left to close.
    if (std::any_of(units.begin(), units.end(), [&slot](const LayoutUnit& unit) { return unit.rect.intersects(slot); }))
        return;

    for (const bool horizontal : {true, false}) {
        std::vector<const LayoutUnit*> after;
        int nearest = std::numeric_limits<int>::max();
        for (const auto& unit : units) {
            const bool later = horizontal
                                   ? unit.rect.getX() >= slot.getRight() && overlapsVertically(unit.rect, slot)
                                   : unit.rect.getY() >= slot.getBottom() && overlapsHorizontally(unit.rect, slot);
            if (!later || unit.pinned)
                continue;
            after.push_back(&unit);
            nearest = std::min(nearest, horizontal ? unit.rect.getX() : unit.rect.getY());
        }
        if (after.empty())
            continue;
        const int wanted = nearest - (horizontal ? slot.getX() : slot.getY());
        const int shift = clearShift(after, units, wanted, horizontal);
        if (shift == 0)
            return;
        for (const auto* unit : after)
            moveUnitBy(unit->key, horizontal ? juce::Point<int>(-shift, 0) : juce::Point<int>(0, -shift));
        refreshAfterMove();
        return;
    }
}
