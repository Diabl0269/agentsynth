// CardGlideAnimatorOrigins.cpp
//
// A copy that comes out of its original (docs/layout/duplicate-motion.md): a duplicated track's macro and modules glide
// from where the original stands to their own place, and the undo of that duplicate goes back into the original.
// Forward it is an ordinary Move glide that also fades up; backward and on redo it is an Exit or Enter ghost that
// travels between its place and the original's rect (`Item::via`) instead of shrinking or growing in place.

#include "CardGlideAnimator.h"

#include "UI/Layout/ReducedMotion.h"

#include <algorithm>

void CardGlideAnimator::noteGlideOutOf(juce::Component* comp, uint32_t nodeUid, juce::Rectangle<int> from) {
    if (depth_ == 0 || comp == nullptr || from.isEmpty() || !canAnimate())
        return;
    if (synth::ui::prefersReducedMotion()) { // nothing travels: the copy fades in where it is
        noteEnter(comp, nodeUid);
        return;
    }
    before_.push_back({juce::Component::SafePointer<juce::Component>(comp), nodeUid, from, true});
}

void CardGlideAnimator::noteOrigin(uint32_t copyKey, uint32_t sourceKey) {
    if (copyKey != 0 && sourceKey != 0)
        origins_[copyKey] = sourceKey;
}

bool CardGlideAnimator::hasFadeUpItems() const noexcept {
    return std::any_of(items_.begin(), items_.end(), [](const Item& it) { return it.kind == Kind::Move && it.fadeUp; });
}

// Points a ghost of a copy at the rect its original occupies now (a card's bounds, or a macro border's hull and chip).
void CardGlideAnimator::attachOrigin(Item& item, const std::vector<Entry>& now,
                                     const std::vector<Border>& bordersNow) const {
    const auto origin = origins_.find(item.nodeUid);
    if (origin == origins_.end())
        return;
    const auto sourceKey = origin->second;
    if (item.border != nullptr) {
        for (const auto& b : bordersNow)
            if (b.open && borderKey(b.key) == sourceKey && !b.hull.isEmpty()) {
                item.via = b.hull.getUnion(b.chip);
                return;
            }
        return;
    }
    for (const auto& e : now)
        if (e.nodeUid == sourceKey && e.comp != nullptr && e.comp->isVisible() && !e.comp->getBounds().isEmpty()) {
            item.via = e.comp->getBounds();
            return;
        }
}

// Where a ghost with an origin is drawn at `progress` (how far an Exit has gone, how far an Enter has come).
juce::Rectangle<float> CardGlideAnimator::viaRect(const Item& item, float progress) const noexcept {
    const auto mix = [progress](juce::Rectangle<int> a, juce::Rectangle<int> b) {
        const auto lerp = [progress](int x, int y) {
            return static_cast<float>(x) + static_cast<float>(y - x) * progress;
        };
        return juce::Rectangle<float>(lerp(a.getX(), b.getX()), lerp(a.getY(), b.getY()),
                                      lerp(a.getWidth(), b.getWidth()), lerp(a.getHeight(), b.getHeight()));
    };
    return item.kind == Kind::Exit ? mix(item.from, *item.via) : mix(*item.via, item.to);
}
