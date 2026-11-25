#include "CycleCountStepper.h"

#include <algorithm>
#include <cmath>

namespace synth::ui {

CycleCountStepper::Count CycleCountStepper::step(int now, int delta, int most) noexcept {
    most = std::max(1, most);
    const int asked = now + delta;
    return {std::clamp(asked, 1, most), asked > most};
}

void CycleCountStepper::begin(float anchorY, int current, int most) noexcept {
    held_ = true;
    moved_ = false;
    anchorY_ = anchorY;
    base_ = std::clamp(current, 1, std::max(1, most));
}

std::optional<CycleCountStepper::Count> CycleCountStepper::dragTo(float y, int most) noexcept {
    most = std::max(1, most);
    const int steps = (int)std::lround((anchorY_ - y) / kStepPx);
    if (steps == 0 && !moved_)
        return std::nullopt;
    moved_ = true;
    // Rebase at the ends, so a step back always changes the count (no hidden steps beyond the limit).
    const bool over = base_ + steps > most;
    if (over)
        base_ = most - steps;
    if (base_ + steps < 1)
        base_ = 1 - steps;
    return Count{std::clamp(base_ + steps, 1, most), over};
}

} // namespace synth::ui
