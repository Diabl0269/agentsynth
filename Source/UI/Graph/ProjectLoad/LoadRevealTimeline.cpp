// LoadRevealTimeline.cpp -- wave slots per level, pending groups, and when cables draw.

#include "LoadRevealTimeline.h"

#include "UI/Layout/UIAnimation.h"
#include <algorithm>

namespace synth::ui::load_reveal {

namespace {
constexpr double kNever = 1.0e12;
}

// The step between levels shrinks with the depth of the patch so the last level always starts within kWaveSpanMs:
// with the pop and the last cable after it, a load where everything is ready lands within about 400 ms.
void Timeline::start(Motion motion, const std::vector<int>& levels, const std::vector<bool>& pending) {
    motion_ = motion;
    int deepest = 0;
    for (int level : levels)
        deepest = std::max(deepest, level);
    step_ = motion == Motion::full && deepest > 0 ? std::min(kMaxLevelStepMs, kWaveSpanMs / deepest) : 0.0;
    slot_.assign(levels.size(), 0.0);
    start_.assign(levels.size(), -1.0);
    for (size_t g = 0; g < levels.size(); ++g) {
        slot_[g] = step_ * std::max(0, levels[g]);
        if (g >= pending.size() || !pending[g])
            start_[g] = slot_[g];
    }
}

void Timeline::markReady(int group, double nowMs) {
    if (group < 0 || group >= groupCount() || start_[(size_t)group] >= 0.0)
        return;
    start_[(size_t)group] = std::max(slot_[(size_t)group], nowMs);
}

bool Timeline::isScheduled(int group) const noexcept {
    return group < 0 || (group < groupCount() && start_[(size_t)group] >= 0.0);
}

bool Timeline::allScheduled() const noexcept {
    return std::all_of(start_.begin(), start_.end(), [](double s) { return s >= 0.0; });
}

float Timeline::popProgress(int group, double nowMs) const noexcept {
    if (!isScheduled(group))
        return 0.0f;
    if (group < 0)
        return 1.0f;
    const double since = nowMs - start_[(size_t)group];
    if (since < 0.0)
        return 0.0f;
    return popMs() <= 0.0 ? 1.0f : (float)std::min(1.0, since / popMs());
}

double Timeline::appearedMs(int group) const noexcept {
    if (group < 0)
        return 0.0;
    if (!isScheduled(group))
        return kNever;
    return start_[(size_t)group] + popMs();
}

float Timeline::cableProgress(int sourceGroup, int destGroup, double nowMs) const noexcept {
    if (sourceGroup < 0 && destGroup < 0)
        return 1.0f; // neither end is part of the reveal (made after it started)
    const double from = std::max(appearedMs(sourceGroup), appearedMs(destGroup));
    if (from >= kNever || nowMs < from)
        return 0.0f;
    if (cableMs() <= 0.0)
        return 1.0f;
    return easeOutCubic((float)std::min(1.0, (nowMs - from) / cableMs()));
}

double Timeline::endMs(const std::vector<std::pair<int, int>>& cables) const noexcept {
    if (!allScheduled())
        return kNever;
    double end = 0.0;
    for (int g = 0; g < groupCount(); ++g)
        end = std::max(end, appearedMs(g));
    for (const auto& [a, b] : cables)
        end = std::max(end, std::max(appearedMs(a), appearedMs(b)) + cableMs());
    return end;
}

} // namespace synth::ui::load_reveal
