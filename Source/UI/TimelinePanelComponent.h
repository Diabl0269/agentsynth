#pragma once

#include "TimelineRulerComponent.h"
#include "TimelineViewState.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {
class TransportService;
}

// TimelinePanelComponent — TL5-1: bottom-docked timeline panel SHELL; TL5-2 fills in the ruler,
// grid, zoom/scroll, snap selector and click-to-seek/drag-to-loop.
//
// MainComponent docks this full-width, above the status bar, toggled via the toolbar button /
// Cmd+T shortcut and slid in/out through the same coordinated AnimationDriver that already
// animates the library/AI panels (see MainComponent::animatePanelTransition()). This class owns
// none of that: it is pure layout + paint, with no timers and no animation of its own.
//
// resized() lays out three regions every task shares the same arithmetic for:
//   - transport bar strip   (top,    Metrics::timelineTransportBarHeight) — houses the snap
//     selector (TL5-2, right-hand side); the rest is empty until TL5-5's transport controls.
//   - track-header column   (left,   Metrics::timelineTrackHeaderWidth)
//   - lanes/ruler area      (remainder) — TimelineRulerComponent (Metrics::timelineRulerHeight)
//     docked at its top, a bar/beat grid painted directly by this component below it.
//
// The single synth::ui::TimelineViewState (beat<->pixel mapping — zoom, scroll, snap) is owned
// here and shared by reference with the ruler; getViewState() exposes it for later tasks (track
// content, playhead) so every consumer maps beats to pixels identically.
//
// Headless-safe: paint()/resized() dynamic_cast<AppLookAndFeel*> and fall back to literal
// values/colours when the themed LnF is absent (test runner has no themed LnF installed).
namespace synth::ui {

class TimelinePanelComponent : public juce::Component {
public:
    TimelinePanelComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Wheel = horizontal scroll; Cmd+wheel (Ctrl on platforms without a Cmd key — mods.isCommandDown()
    // already abstracts this) = zoom around the cursor. Implemented once here (rather than
    // separately on the ruler) so the ruler and the lanes grid share identical behaviour — JUCE
    // bubbles an unhandled wheel event from the ruler child up to this override.
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Non-owning; may be null (tests, or before MainComponent finishes wiring). Forwarded to the
    // ruler, which is the only sub-component that talks to the transport directly.
    void setTransport(synth::TransportService* transport);
    // Non-owning. Restores/persists the snap-selector choice under the "timelineSnap" key, same
    // pattern as AIChatComponent::setAccountService()'s non-owning setter.
    void setApplicationProperties(juce::ApplicationProperties* props);

    // Pure geometry getters — later tasks and tests build on the same rects rather than
    // re-deriving the arithmetic in resized().
    juce::Rectangle<int> getTransportBarBounds() const noexcept { return transportBarBounds_; }
    juce::Rectangle<int> getTrackHeaderBounds() const noexcept { return trackHeaderBounds_; }
    juce::Rectangle<int> getLanesBounds() const noexcept { return lanesBounds_; }

    TimelineViewState& getViewState() noexcept { return viewState_; }
    TimelineRulerComponent& getRuler() noexcept { return ruler_; }
    juce::ComboBox& getSnapCombo() noexcept { return snapCombo_; }

private:
    void persistSnapChoice();

    TimelineViewState viewState_;
    TimelineRulerComponent ruler_{viewState_};
    juce::ComboBox snapCombo_;

    juce::ApplicationProperties* appProperties_ = nullptr;

    juce::Rectangle<int> transportBarBounds_;
    juce::Rectangle<int> trackHeaderBounds_;
    juce::Rectangle<int> lanesBounds_;
    juce::Rectangle<int> gridLanesBounds_; // lanesBounds_ minus the ruler strip at its top

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelinePanelComponent)
};

} // namespace synth::ui
