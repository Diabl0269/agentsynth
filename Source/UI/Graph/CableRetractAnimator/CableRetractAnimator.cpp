// CableRetractAnimator.cpp -- which cables become ghosts, and where a ghost's end is drawn as it retracts.

#include "CableRetractAnimator.h"
#include "UI/Layout/UIAnimation.h"

bool CableRetractAnimator::arm(const std::vector<VisibleCable>& before, const std::vector<VisibleCable>& after) {
    ghosts_.clear();
    progress_ = 0.0f;
    for (const auto& was : before) {
        bool stillThere = false;
        for (const auto& now : after)
            if (now.id == was.id) {
                stillThere = true;
                break;
            }
        if (!stillThere)
            ghosts_.push_back(was);
    }
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
