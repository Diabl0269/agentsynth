#pragma once

// Owns the window's one toast and decides where it sits: every source names the surface where its action happened
// (docs/layout/chrome.md#toast).

#include "UI/Chrome/Toast/ToastComponent.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

/** Where a toast sits: over the canvas, or over the open timeline (the canvas when the timeline is not on screen). */
enum class ToastSurface { canvas, timeline };

class ToastHost {
public:
    /** `timelineArea` gives the timeline's bounds in the parent's coordinates, empty when it is not on screen. */
    ToastHost(juce::Component& parent, std::function<juce::Rectangle<int>()> timelineArea);

    /** The layout pass says where the canvas is; the toast re-places itself on its current surface. */
    void setCanvasArea(juce::Rectangle<int> area);
    /** Shows a toast on `surface` (a second one replaces the first in place). Message thread only. */
    void show(ToastSurface surface, const juce::String& message, const juce::String& actionLabel = {},
              std::function<void()> action = {}, const juce::String& actionTooltip = {},
              ui::ToastComponent::Severity severity = ui::ToastComponent::Severity::info);
    ui::ToastComponent& view() noexcept { return toast_; }

private:
    void place();

    ui::ToastComponent toast_;
    std::function<juce::Rectangle<int>()> timelineArea_;
    ToastSurface surface_ = ToastSurface::canvas;
    juce::Rectangle<int> canvasArea_;
};

} // namespace synth
