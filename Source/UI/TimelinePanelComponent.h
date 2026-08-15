#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// TimelinePanelComponent — TL5-1: bottom-docked timeline panel SHELL.
//
// MainComponent docks this full-width, above the status bar, toggled via the toolbar button /
// Cmd+T shortcut and slid in/out through the same coordinated AnimationDriver that already
// animates the library/AI panels (see MainComponent::animatePanelTransition()). This class owns
// none of that: it is pure layout + paint, with no timers and no animation of its own.
//
// For TL5-1 this is only the frame later tasks (TL5-2+) build content into. resized() lays out
// three placeholder regions every future task shares the same arithmetic for:
//   - transport bar strip   (top,    Metrics::timelineTransportBarHeight)
//   - track-header column   (left,   Metrics::timelineTrackHeaderWidth)
//   - lanes/ruler area      (remainder) — paints the "Timeline" placeholder text
//
// Headless-safe: paint()/resized() dynamic_cast<AppLookAndFeel*> and fall back to literal
// values/colours when the themed LnF is absent (test runner has no themed LnF installed).
namespace synth::ui {

class TimelinePanelComponent : public juce::Component {
public:
    TimelinePanelComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Pure geometry getters — later tasks and tests build on the same rects rather than
    // re-deriving the arithmetic in resized().
    juce::Rectangle<int> getTransportBarBounds() const noexcept { return transportBarBounds_; }
    juce::Rectangle<int> getTrackHeaderBounds() const noexcept { return trackHeaderBounds_; }
    juce::Rectangle<int> getLanesBounds() const noexcept { return lanesBounds_; }

private:
    juce::Rectangle<int> transportBarBounds_;
    juce::Rectangle<int> trackHeaderBounds_;
    juce::Rectangle<int> lanesBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelinePanelComponent)
};

} // namespace synth::ui
