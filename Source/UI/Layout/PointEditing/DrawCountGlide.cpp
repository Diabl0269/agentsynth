#include "DrawCountGlide.h"
#include <utility>

namespace synth::ui {

void DrawCountGlide::begin(Line drawnNow, bool lineIsFunctionOfX) {
    if (drawnNow.empty()) {
        clear();
        return;
    }
    from_ = std::move(drawnNow);
    stepped_ = lineIsFunctionOfX;
    tween_.retarget(0, {0.0f, 1.0f}, {0.0f, 0.0f});
}

void DrawCountGlide::clear() {
    tween_.clear();
    from_.clear();
}

float DrawCountGlide::weight() const { return from_.empty() ? 0.0f : juce::jlimit(0.0f, 1.0f, tween_.offsetFor(0).y); }

DrawCountGlide::Line DrawCountGlide::drawn(const Line& resting) const {
    const float w = weight();
    if (w <= 0.0f || resting.empty())
        return resting;
    return stepped_ ? line_morph::blendAcrossSteps(from_, resting, w) : line_morph::blend(from_, resting, w);
}

} // namespace synth::ui
