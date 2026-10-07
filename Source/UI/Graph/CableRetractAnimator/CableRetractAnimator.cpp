// CableRetractAnimator.cpp -- which cables become ghosts, and where a ghost's end is drawn as it retracts.

#include "CableRetractAnimator.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <tuple>

// The cables still drawn are looked up in a set built once, so the diff costs what the two lists hold, never the
// product of their sizes: on a project with thousands of cables a scan of `after` per cable in `before` made every
// undo's retract diff grow with the square of the project.
bool CableRetractAnimator::arm(const std::vector<VisibleCable>& before, const std::vector<VisibleCable>& after) {
    ghosts_.clear();
    progress_ = 0.0f;
    using Key = std::tuple<uint32_t, int, uint32_t, int, uint32_t>;
    const auto key = [](const graph_editor_types::CableId& id) {
        return Key{id.srcUid, id.srcPort, id.dstUid, id.dstPort, id.attenUid};
    };
    std::vector<Key> stillDrawn;
    stillDrawn.reserve(after.size());
    for (const auto& now : after)
        stillDrawn.push_back(key(now.id));
    std::sort(stillDrawn.begin(), stillDrawn.end());
    for (const auto& was : before)
        if (!std::binary_search(stillDrawn.begin(), stillDrawn.end(), key(was.id)))
            ghosts_.push_back(was);
    return isLive();
}

void CableRetractAnimator::applyTweenAt(float t) noexcept { progress_ = juce::jlimit(0.0f, 1.0f, t); }

void CableRetractAnimator::finish() noexcept {
    ghosts_.clear();
    progress_ = 1.0f;
}

// Ease-in: a thing leaving starts slowly and speeds away (docs/layout/animation.md, motion rules).
std::vector<CableRetractAnimator::VisibleCable> CableRetractAnimator::ghosts() const {
    const float pulled = synth::ui::easeInCubic(progress_);
    auto drawn = ghosts_;
    for (auto& cable : drawn)
        cable.p2 += (cable.p1 - cable.p2) * pulled;
    return drawn;
}
