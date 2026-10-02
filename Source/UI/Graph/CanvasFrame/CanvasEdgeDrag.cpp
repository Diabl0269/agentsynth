#include "CanvasEdgeDrag.h"

#include "UI/Layout/LayoutUtil.h"

void CanvasEdgeDrag::begin(juce::Rectangle<int> movingUnion, juce::Point<int> floor,
                           std::set<juce::String> movingKeys) {
    reset();
    if (movingUnion.isEmpty() || movingKeys.empty())
        return;
    armed_ = true;
    startUnion_ = movingUnion;
    floor_ = floor;
    movingKeys_ = std::move(movingKeys);
}

juce::Point<int> CanvasEdgeDrag::clampDelta(juce::Point<int> raw) {
    if (!armed_)
        return raw;
    const juce::Point<int> clamped{juce::jmax(raw.x, floor_.x - startUnion_.getX()),
                                   juce::jmax(raw.y, floor_.y - startUnion_.getY())};
    overshoot_ = clamped - raw;
    return clamped;
}

juce::Point<int> CanvasEdgeDrag::takeOvershoot() {
    constexpr int g = synth::LayoutUtil::kGridSize;
    const juce::Point<int> d{(overshoot_.x + g - 1) / g * g, (overshoot_.y + g - 1) / g * g};
    overshoot_ = {};
    return d;
}
