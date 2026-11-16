#include "ToastHost.h"

namespace synth {

ToastHost::ToastHost(juce::Component& parent, std::function<juce::Rectangle<int>()> timelineArea)
    : timelineArea_(std::move(timelineArea)) {
    parent.addChildComponent(toast_);
}

void ToastHost::setCanvasArea(juce::Rectangle<int> area) {
    canvasArea_ = area;
    place();
}

void ToastHost::place() {
    auto area = canvasArea_;
    if (surface_ == ToastSurface::timeline && timelineArea_) {
        const auto timeline = timelineArea_();
        if (!timeline.isEmpty())
            area = timeline;
    }
    toast_.placeIn(area);
}

void ToastHost::show(ToastSurface surface, const juce::String& message, const juce::String& actionLabel,
                     std::function<void()> action, const juce::String& actionTooltip,
                     ui::ToastComponent::Severity severity) {
    surface_ = surface;
    place();
    toast_.show(message, actionLabel, std::move(action), actionTooltip, severity);
}

} // namespace synth
