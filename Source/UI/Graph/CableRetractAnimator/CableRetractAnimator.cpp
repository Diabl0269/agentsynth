// CableRetractAnimator.cpp -- which cables become ghosts or grow, and where their ends are drawn as they move.

#include "CableRetractAnimator.h"
#include "UI/Layout/CableCurve.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <tuple>

namespace {
using Key = std::tuple<uint32_t, int, uint32_t, int, uint32_t>;
Key keyOf(const graph_editor_types::CableId& id) {
    return Key{id.srcUid, id.srcPort, id.dstUid, id.dstPort, id.attenUid};
}

std::vector<Key> sortedKeys(const std::vector<graph_editor_types::VisibleCable>& cables) {
    std::vector<Key> keys;
    keys.reserve(cables.size());
    for (const auto& cable : cables)
        keys.push_back(keyOf(cable.id));
    std::sort(keys.begin(), keys.end());
    return keys;
}
} // namespace

// The cables on the other side of the change are looked up in a set built once, so the diff costs what the two lists
// hold, never the product of their sizes: on a project with thousands of cables a scan of `after` per cable in
// `before` made every undo's diff grow with the square of the project.
bool CableRetractAnimator::arm(const std::vector<VisibleCable>& before, const std::vector<VisibleCable>& after,
                               Options options) {
    ghosts_.clear();
    growing_.clear();
    progress_ = 0.0f;
    reduced_ = options.reduceMotion;
    const auto stillDrawn = sortedKeys(after);
    for (const auto& was : before)
        if (!std::binary_search(stillDrawn.begin(), stillDrawn.end(), keyOf(was.id)))
            ghosts_.push_back(was);
    if (options.growAdded) {
        const auto wasDrawn = sortedKeys(before);
        for (const auto& now : after)
            if (!std::binary_search(wasDrawn.begin(), wasDrawn.end(), keyOf(now.id)))
                growing_.push_back(now);
        if (growing_.size() > kMaxGrowing)
            growing_.clear();
    }
    return isLive();
}

void CableRetractAnimator::applyTweenAt(float t) noexcept { progress_ = juce::jlimit(0.0f, 1.0f, t); }

void CableRetractAnimator::finish() noexcept {
    ghosts_.clear();
    growing_.clear();
    progress_ = 1.0f;
}

// Ease in and out: the cable gathers itself, pulls in, and settles into the jack (docs/layout/animation.md).
std::vector<CableRetractAnimator::VisibleCable> CableRetractAnimator::ghosts() const {
    auto drawn = ghosts_;
    if (reduced_)
        return drawn;
    const float pulled = synth::ui::easeInOutCubic(progress_);
    for (auto& cable : drawn)
        cable.p2 += (cable.p1 - cable.p2) * pulled;
    return drawn;
}

float CableRetractAnimator::opacity() const noexcept {
    if (reduced_)
        return 1.0f - progress_;
    return progress_ <= kRetractHoldUntil ? 1.0f : 1.0f - (progress_ - kRetractHoldUntil) / (1.0f - kRetractHoldUntil);
}

bool CableRetractAnimator::isGrowing(const graph_editor_types::CableId& id) const noexcept {
    return std::any_of(growing_.begin(), growing_.end(), [&id](const VisibleCable& c) { return c.id == id; });
}

// Ease out: it shoots out of the jack and settles onto the destination.
CableRetractAnimator::VisibleCable CableRetractAnimator::grown(const VisibleCable& live) const noexcept {
    auto drawn = live;
    if (!reduced_)
        drawn.p2 = live.p1 + (live.p2 - live.p1) * synth::ui::easeOutCubic(progress_);
    return drawn;
}

float CableRetractAnimator::growOpacity() const noexcept {
    return reduced_ ? progress_ : juce::jmin(1.0f, progress_ / kGrowFadeInUntil);
}

juce::Rectangle<int> CableRetractAnimator::paintArea() const {
    juce::Rectangle<float> area;
    for (const auto& ghost : ghosts())
        area = area.getUnion(synth::ui::cablePaintBounds(ghost.p1, ghost.p2));
    for (const auto& cable : growing_) {
        const auto now = grown(cable);
        area = area.getUnion(synth::ui::cablePaintBounds(now.p1, now.p2));
    }
    return area.getSmallestIntegerContainer();
}
