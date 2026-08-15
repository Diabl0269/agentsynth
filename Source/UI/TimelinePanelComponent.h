#pragma once

#include "../Timeline/TimelineDoc.h"
#include "ClipSelectionModel.h"
#include "TimelineClipLaneArea.h"
#include "TimelinePlayheadOverlay.h"
#include "TimelineRulerComponent.h"
#include "TimelineTrackHeaderComponent.h"
#include "TimelineTransportBar.h"
#include "TimelineViewState.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth {
class TransportService;
class Metronome; // Forward declaration (Source/Transport/Metronome.h)
} // namespace synth

// TimelinePanelComponent — TL5-1: bottom-docked timeline panel SHELL; TL5-2 fills in the ruler,
// grid, zoom/scroll, snap selector and click-to-seek/drag-to-loop.
//
// MainComponent docks this full-width, above the status bar, toggled via the toolbar button /
// Cmd+T shortcut and slid in/out through the same coordinated AnimationDriver that already
// animates the library/AI panels (see MainComponent::animatePanelTransition()). This class owns
// none of that: it is layout + paint, with no timer and no animation of its own — TL5-4's
// updateFromTransport() is driven by MainComponent's EXISTING 10 Hz timer, and the only timer
// anywhere under this panel is the playhead overlay's, which runs only while the transport plays
// (see TimelinePlayheadOverlay.h and docs/layout.md §11).
//
// resized() lays out three regions every task shares the same arithmetic for:
//   - transport bar strip   (top,    Metrics::timelineTransportBarHeight) — houses the snap
//     selector (TL5-2, right-hand side) and TL5-5's synth::ui::TimelineTransportBar (play/stop/
//     record/loop + BPM/time-sig editors + the bar:beat readout), left-aligned in the rest.
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
    // ruler and the playhead overlay — the two sub-components that talk to the transport directly
    // — and, TL5-7, to the clip-lane area (it only reads the time signature, for Snap::Bar).
    void setTransport(synth::TransportService* transport);

    // TL5-6: forwarded straight to the transport bar's own metronome toggle — see
    // TimelineTransportBar::setMetronome. Non-owning; may be null.
    void setMetronome(synth::Metronome* metronome);

    // TL5-4: THE low-rate transport poll, called from MainComponent's EXISTING 10 Hz timer (gated
    // #if SYNTH_ENABLE_TIMELINE, and only while this panel is visible). It adds no timer of its own.
    // Two jobs:
    //   - hand the snapshot to the playhead overlay, which owns its 30 Hz playing-only timer from
    //     the play/stop transitions it sees here (see TimelinePlayheadOverlay.h);
    //   - diff the small slice of transport state the RULER paints (time signature + loop trio) and
    //     repaint it only on a change. The position is deliberately NOT part of that diff: the
    //     playhead is the only thing that moves with it, and it repaints its own strip. Same gated
    //     idiom as the status bar's 5 Hz poll.
    void updateFromTransport(const synth::TransportService::PositionSnapshot& snapshot, double outputLatencySeconds);

    // Non-owning. Restores/persists the snap-selector choice under the "timelineSnap" key, same
    // pattern as AIChatComponent::setAccountService()'s non-owning setter. Also forwarded to the
    // transport bar (TL5-6), which restores/persists ITS OWN two keys ("timelineMetronomeEnabled",
    // "timelineCountInBars") — this panel has no other reason to know either setting exists, so it
    // is a pure forward, not a third copy of the restore/persist idiom.
    void setApplicationProperties(juce::ApplicationProperties* props);

    // TL5-3. Non-owning; may be null (a SYNTH_ENABLE_TIMELINE=OFF build never sets one, and the
    // panel is then an inert shell with an empty header column). The panel listens to the doc and
    // rebuilds/refreshes the track headers on every notification — that is the ONLY thing that
    // updates them: no timer, no polling. TL5-7: also forwarded to the clip-lane area, which runs
    // the same "set doc, refresh once" seam (TimelineClipLaneArea::setTimelineDoc).
    void setTimelineDoc(synth::TimelineDoc* doc);
    synth::TimelineDoc* getTimelineDoc() const noexcept { return doc_; }

    // Non-owning. Handed to every track header (and driven by the "+ MIDI Track" button), so the
    // header column's whole conversation with the app goes through one seam. Must be set before
    // (or at the same time as) setTimelineDoc for the first build to be fully wired.
    void setTrackHeaderHost(TrackHeaderHost* host);

    // TL5-7: forwarded to the clip-lane area — MainComponent's existing AppUndoManager is what
    // makes every clip drag/trim/split/duplicate/delete ONE undo step. Non-owning; may be null
    // (mutations then apply directly, off the undo stack — same degrade-gracefully contract every
    // other non-owning setter here has).
    void setUndoManager(AppUndoManager* undoManager);

    // The clip lane area and the selection model behind it (TL5-7). The panel owns the selection
    // model; the lane area only holds a reference to it (see TimelineClipLaneArea's ctor).
    synth::ui::ClipSelectionModel& getClipSelection() noexcept { return clipSelection_; }
    synth::ui::TimelineClipLaneArea& getClipLaneArea() noexcept { return clipLaneArea_; }

    // Pure geometry getters — later tasks and tests build on the same rects rather than
    // re-deriving the arithmetic in resized().
    juce::Rectangle<int> getTransportBarBounds() const noexcept { return transportBarBounds_; }
    // The WHOLE left column, including the "+ MIDI Track" strip at its top — the three regions
    // still tile the panel exactly (see TimelinePanelComponentTest.PanelRegionsTile).
    juce::Rectangle<int> getTrackHeaderBounds() const noexcept { return trackHeaderBounds_; }
    juce::Rectangle<int> getLanesBounds() const noexcept { return lanesBounds_; }

    TimelineViewState& getViewState() noexcept { return viewState_; }
    TimelineRulerComponent& getRuler() noexcept { return ruler_; }
    TimelinePlayheadOverlay& getPlayhead() noexcept { return playhead_; }
    juce::ComboBox& getSnapCombo() noexcept { return snapCombo_; }
    // TL5-5: play/stop/record/loop + BPM/time-sig editors + the bar:beat readout.
    TimelineTransportBar& getTransportBar() noexcept { return transportBar_; }

    /** How many times updateFromTransport() has been called. Test hook: it is what proves the
     *  10 Hz poll never reaches a hidden panel. */
    int getTransportUpdateCountForTest() const noexcept { return transportUpdateCount_; }

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
    // TimelineDoc::Listener — the single trigger for a header rebuild/refresh, AND (TL5-7) the
    // clip-lane area's refresh (prunes the clip selection of anything the mutation removed).
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
    // TL5-7: the panel owns the clip selection; the lane area only holds a reference to it (same
    // relationship it has with viewState_ below).
    synth::ui::ClipSelectionModel clipSelection_;
    TimelineRulerComponent ruler_{viewState_};
    // Positioned over gridLanesBounds_ in resized() — the SAME rect the grid below it is painted
    // into by this component's own paint(). Added as a child AFTER the grid is painted (parent
    // paint() always precedes children) and BEFORE playhead_ (added last, below), so z-order reads
    // grid -> clips -> playhead with no second place ever painting the grid.
    synth::ui::TimelineClipLaneArea clipLaneArea_{viewState_, clipSelection_};
    // Added LAST in the constructor so it sits on top of the ruler AND the clip lane area; spans
    // ruler + lanes and intercepts no mouse clicks (see TimelinePlayheadOverlay's ctor).
    TimelinePlayheadOverlay playhead_{viewState_};
    juce::ComboBox snapCombo_;
    // TL5-5: left-aligned in the transport-bar strip, the snap combo stays right of it.
    TimelineTransportBar transportBar_;

    // The slice of transport state the RULER paints, diffed by updateFromTransport. `hasRulerState_`
    // keeps the very first poll from counting as a change (the default-constructed struct below is
    // not what a live transport reports — its loop end starts at 4 beats).
    struct RulerTransportState {
        int timeSigNumerator = 4;
        int timeSigDenominator = 4;
        bool looping = false;
        double loopStartPpq = 0.0;
        double loopEndPpq = 0.0;

        bool operator==(const RulerTransportState& other) const noexcept {
            return timeSigNumerator == other.timeSigNumerator && timeSigDenominator == other.timeSigDenominator &&
                   looping == other.looping && loopStartPpq == other.loopStartPpq && loopEndPpq == other.loopEndPpq;
        }
    };
    RulerTransportState rulerState_;
    bool hasRulerState_ = false;
    int transportUpdateCount_ = 0;

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
