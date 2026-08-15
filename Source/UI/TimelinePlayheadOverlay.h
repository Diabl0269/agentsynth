#pragma once

#include "../Transport/TransportService.h"
#include "TimelineViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>

// TimelinePlayheadOverlay — TL5-4: the vertical position line drawn over the timeline panel's
// ruler + lanes region.
//
// This is the SECOND (and, with the AI thinking spinner, the only other) documented exception to
// the app's no-unconditional-per-tick-repaint rule — see docs/layout.md §11 and the CLAUDE.md
// invariant. The exception is granted under a confinement contract, and every clause of it is
// enforced here:
//
//   1. PLAYING ONLY. The 30 Hz juce::Timer is started on the play transition and stopped on the
//      stop/pause transition. It NEVER runs while the transport is stopped, so an idle app repaints
//      nothing at all.
//   2. STRIP ONLY. A tick never repaints the component: it repaints the union of the OLD and NEW
//      line strips (each kStripHalfWidth px either side of the line), clipped to the bounds. A
//      tick where the rounded x did not move requests nothing.
//   3. EXPLICIT STOP. Stopping emits exactly ONE final strip repaint (so the line settles on the
//      stop position) and then goes silent.
//
// Transitions are NOT detected here. The owner polls the transport at a low rate (MainComponent's
// existing 10 Hz timer -> TimelinePanelComponent::updateFromTransport) and hands the snapshot in;
// this class owns the 30 Hz timer's lifecycle from those calls alone. Keeping one owner for the
// lifecycle is why the 30 Hz tick itself never stops the timer: it only re-reads the transport
// (through the stored, non-owning TransportService*) so the line moves smoothly between polls.
//
// The DRAWN position is deliberately not the transport position: it is offset backwards by the
// audio device's output latency (`ppq - outputLatencySeconds * bpm/60`, clamped >= 0), so the line
// lines up with what is being HEARD rather than with the block currently being rendered.
//
// Headless-safe: paint() dynamic_cast<AppLookAndFeel*>s and falls back to a literal colour, and no
// path here needs a peer, a device or a message loop.
namespace synth::ui {

class TimelinePlayheadOverlay
    : public juce::Component
    , private juce::Timer {
public:
    // The playing-only frame rate. 30 Hz matches the GraphEditor animation tick — fast enough that
    // the line reads as continuous motion, and the repaint it costs is a few-pixel-wide strip.
    static constexpr int kFrameRateHz = 30;
    // Line thickness, and the margin either side of it that a repaint strip must cover (the strip
    // has to include the pixels the ANTI-ALIASED line touched, not just its nominal width).
    static constexpr float kLineWidth = 2.0f;
    static constexpr int kStripHalfWidth = 2;

    explicit TimelinePlayheadOverlay(TimelineViewState& viewState);
    ~TimelinePlayheadOverlay() override = default;

    // Non-owning; may be null (tests, or before the panel is wired). Only the 30 Hz tick reads it,
    // and only to re-read the position between the owner's low-rate polls.
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    synth::TransportService* getTransport() const noexcept { return transport_; }

    // THE drive seam. Called from the owner's LOW-RATE poll (10 Hz), never faster: it stores the
    // snapshot, starts/stops the 30 Hz timer on a play/stop transition, and requests a strip repaint
    // only when the rounded line x actually moved (plus the one guaranteed final strip on stop).
    void updateFromTransport(const synth::TransportService::PositionSnapshot& snapshot, double outputLatencySeconds);

    void paint(juce::Graphics& g) override;

    // The x (in this component's coordinates, which share TimelineViewState's origin) the line is
    // drawn at RIGHT NOW, latency offset included. Recomputed from the stored snapshot + view state
    // on every call — it is not cached, so a zoom/scroll changes it immediately.
    int getLineX() const noexcept;

    // The x the line was last DRAWN/REQUESTED at, in PIXEL space. Deliberately not re-derived from
    // the old beat: after a zoom the old beat maps somewhere new, but the stale pixels that must be
    // repainted are where the line actually was. This is what the old half of the union strip is
    // built from.
    int getLastRequestedLineX() const noexcept { return lineX_; }

    bool isPlayheadTimerRunning() const noexcept { return isTimerRunning(); }

protected:
    // THE paint-count seam, and the repo's pattern for making a repaint budget testable: EVERY
    // repaint this component asks for goes through here and nowhere else. A test subclasses the
    // component, overrides this to count calls (and record the rect), and can then assert an exact
    // repaint budget headlessly — no peer, no message loop, no screenshot diffing. The default
    // implementation is the real repaint.
    virtual void requestRepaintStrip(juce::Rectangle<int> strip);

    // juce::Timer, protected so a test subclass can drive frames deterministically instead of
    // waiting on wall-clock time. Only ever runs while playing.
    void timerCallback() override;

private:
    // The repaint strip for a line at `x`: the line plus kStripHalfWidth px either side.
    juce::Rectangle<int> stripFor(int x) const noexcept;

    // Recomputes the line x and requests the union strip when it moved. `force` (the stop
    // transition) requests one strip even when it did not.
    void refreshLine(bool force);

    TimelineViewState& viewState_;
    synth::TransportService* transport_ = nullptr;

    synth::TransportService::PositionSnapshot snapshot_{};
    double outputLatencySeconds_ = 0.0;

    bool playing_ = false;
    // Seeded (without requesting a repaint) by the first updateFromTransport: before that there is
    // no "old" line on screen to erase, so the first poll of a stopped transport must stay silent.
    bool hasLineX_ = false;
    int lineX_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelinePlayheadOverlay)
};

} // namespace synth::ui
