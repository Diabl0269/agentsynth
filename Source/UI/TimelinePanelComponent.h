#pragma once

#include "../Timeline/TimelineDoc.h"
#include "TimelineRulerComponent.h"
#include "TimelineTrackHeaderComponent.h"
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

class TimelinePanelComponent
    : public juce::Component
    , private synth::TimelineDoc::Listener {
public:
    TimelinePanelComponent();
    ~TimelinePanelComponent() override;

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

    // TL5-3. Non-owning; may be null (a SYNTH_ENABLE_TIMELINE=OFF build never sets one, and the
    // panel is then an inert shell with an empty header column). The panel listens to the doc and
    // rebuilds/refreshes the track headers on every notification — that is the ONLY thing that
    // updates them: no timer, no polling.
    void setTimelineDoc(synth::TimelineDoc* doc);
    synth::TimelineDoc* getTimelineDoc() const noexcept { return doc_; }

    // Non-owning. Handed to every track header (and driven by the "+ MIDI Track" button), so the
    // header column's whole conversation with the app goes through one seam. Must be set before
    // (or at the same time as) setTimelineDoc for the first build to be fully wired.
    void setTrackHeaderHost(TrackHeaderHost* host);

    // Pure geometry getters — later tasks and tests build on the same rects rather than
    // re-deriving the arithmetic in resized().
    juce::Rectangle<int> getTransportBarBounds() const noexcept { return transportBarBounds_; }
    // The WHOLE left column, including the "+ MIDI Track" strip at its top — the three regions
    // still tile the panel exactly (see TimelinePanelComponentTest.PanelRegionsTile).
    juce::Rectangle<int> getTrackHeaderBounds() const noexcept { return trackHeaderBounds_; }
    juce::Rectangle<int> getLanesBounds() const noexcept { return lanesBounds_; }

    TimelineViewState& getViewState() noexcept { return viewState_; }
    TimelineRulerComponent& getRuler() noexcept { return ruler_; }
    juce::ComboBox& getSnapCombo() noexcept { return snapCombo_; }

    // ---- Track headers (TL5-3) ----
    juce::TextButton& getAddTrackButton() noexcept { return addTrackButton_; }
    juce::Viewport& getTrackHeaderViewport() noexcept { return trackHeaderViewport_; }
    int getTrackHeaderCount() const noexcept { return trackHeaderList_.headers.size(); }
    /** Header for the track at `index` in the doc's track order, or nullptr when out of range. */
    TimelineTrackHeaderComponent* getTrackHeaderAt(int index) const noexcept {
        return juce::isPositiveAndBelow(index, trackHeaderList_.headers.size())
                   ? trackHeaderList_.headers.getUnchecked(index)
                   : nullptr;
    }

private:
    // TimelineDoc::Listener — the single trigger for a header rebuild/refresh.
    void timelineChanged(const synth::TimelineDoc& doc) override;

    void persistSnapChoice();
    // Rebuilds the header components when the set of track ids changed, and otherwise just
    // refreshes the existing ones in place (a mute toggle must not destroy and re-create rows).
    void syncTrackHeaders();
    void layoutTrackHeaders();

    // The Viewport's content: a plain container whose height is (track count * row height).
    struct TrackHeaderList : juce::Component {
        juce::OwnedArray<TimelineTrackHeaderComponent> headers;
    };

    TimelineViewState viewState_;
    TimelineRulerComponent ruler_{viewState_};
    juce::ComboBox snapCombo_;

    juce::ApplicationProperties* appProperties_ = nullptr;
    synth::TimelineDoc* doc_ = nullptr;
    TrackHeaderHost* trackHeaderHost_ = nullptr;

    juce::TextButton addTrackButton_{"+ MIDI Track"};
    juce::Viewport trackHeaderViewport_;
    TrackHeaderList trackHeaderList_;

    juce::Rectangle<int> transportBarBounds_;
    juce::Rectangle<int> trackHeaderBounds_;
    juce::Rectangle<int> lanesBounds_;
    juce::Rectangle<int> gridLanesBounds_; // lanesBounds_ minus the ruler strip at its top

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelinePanelComponent)
};

} // namespace synth::ui
