#pragma once

#include <algorithm>
#include <cmath>

// Pure, headless-testable beat<->pixel mapping shared by the timeline ruler and the lanes grid
// (and, later, track content). No JUCE dependency and no component lifetime: TimelinePanelComponent
// owns the single instance and hands a reference to TimelineRulerComponent (getViewState() exposes
// it), so every consumer maps beats to pixels identically and there is exactly one place
// zoom/scroll/snap state lives.
namespace synth::ui {

struct TimelineViewState {
    // Zoom clamp bounds (pixels per beat). 1.5 keeps a many-minute arrangement scrollable in a
    // panel-width view; 512 is close enough to sample-accurate editing for a beat-grid timeline.
    static constexpr double kMinPixelsPerBeat = 1.5;
    static constexpr double kMaxPixelsPerBeat = 512.0;

    double pixelsPerBeat = 24.0;   // zoom; always clamped to [kMinPixelsPerBeat, kMaxPixelsPerBeat]
    double firstVisibleBeat = 0.0; // scroll; the beat at local x == 0, always clamped >= 0

    double beatToX(double beat) const noexcept { return (beat - firstVisibleBeat) * pixelsPerBeat; }
    double xToBeat(double x) const noexcept { return firstVisibleBeat + x / pixelsPerBeat; }

    // Zooms by `factor` (> 1 zooms in, < 1 zooms out, 1 is a no-op) while keeping the beat
    // currently under `anchorX` fixed on screen: the anchor beat is read BEFORE pixelsPerBeat
    // changes, then firstVisibleBeat is re-derived so the (possibly clamped) new pixelsPerBeat
    // still maps anchorX back to that same beat. When the firstVisibleBeat >= 0 clamp is what
    // actually engages (deep zoom-out past beat 0 under the cursor), the anchor invariant yields
    // to the clamp by design — there is no valid firstVisibleBeat that satisfies both.
    void zoomAroundX(double factor, double anchorX) noexcept {
        const double anchorBeat = xToBeat(anchorX);
        pixelsPerBeat = std::clamp(pixelsPerBeat * factor, kMinPixelsPerBeat, kMaxPixelsPerBeat);
        firstVisibleBeat = std::max(0.0, anchorBeat - anchorX / pixelsPerBeat);
    }

    void scrollBeats(double deltaBeats) noexcept { firstVisibleBeat = std::max(0.0, firstVisibleBeat + deltaBeats); }

    // ---- Vertical track geometry, shared by the header column and the lanes grid ----
    // rowHeightScale multiplies the themed row height (vertical zoom); trackScrollY is how many
    // pixels of track rows are scrolled off the top. Both live here (not on the panel) for the
    // same reason the horizontal mapping does: the headers and the lanes must read ONE value or
    // their rows drift apart.
    static constexpr double kMinRowHeightScale = 0.5;
    static constexpr double kMaxRowHeightScale = 3.0;
    double rowHeightScale = 1.0;
    double trackScrollY = 0.0; // px, always clamped >= 0

    void scaleRowHeight(double factor) noexcept {
        if (!std::isfinite(factor) || factor <= 0.0)
            return;
        rowHeightScale = std::clamp(rowHeightScale * factor, kMinRowHeightScale, kMaxRowHeightScale);
    }

    // maxScrollPx is (total rows height - visible lanes height), computed by the caller — this
    // struct deliberately knows nothing about track counts or component heights.
    void scrollTracksPx(double deltaPx, double maxScrollPx) noexcept {
        trackScrollY = std::clamp(trackScrollY + deltaPx, 0.0, std::max(0.0, maxScrollPx));
    }

    // Snap division, expressed as a NOTE VALUE (a fraction of a whole note), the way every DAW's
    // grid selector reads: "1" is a whole note (4 beats — a full 4/4 bar), "1/4" is a quarter note
    // (1 beat), "1/16" is a sixteenth (0.25 beat). A beat is always a quarter note here
    // (TransportService's own convention), so these are fixed beat counts; only Snap::Bar consults
    // the time signature.
    // The order is load-bearing: Bar..Sixteenth are declared COARSEST to FINEST, which is what lets
    // TimelinePanelComponent::cycleSnapValue step the grid with a clamped +-1 on the underlying int
    // instead of carrying a second table that could drift out of sync with this list.
    enum class Snap : int { Off = 0, Bar, Whole, Half, Quarter, Eighth, Sixteenth };
    Snap snap = Snap::Quarter;

    // The last non-Off division that was chosen, so "cycle the grid finer/coarser" has somewhere
    // musical to land when the current value is Off — see cycleSnapValue's from-Off rule.
    // Deliberately NOT persisted: it is a within-session convenience, and a fresh launch that
    // restored Snap::Off should re-enter the cycle at the documented default rather than at
    // whatever division a session weeks ago happened to end on.
    Snap lastMusicalSnap = Snap::Quarter;

    // The one writer for `snap` that keeps lastMusicalSnap in step. Direct assignment to `snap`
    // stays legal (tests and the doc-load path set it wholesale), it just doesn't feed the cycle's
    // memory — which is exactly right for a value the user never picked.
    void setSnap(Snap value) noexcept {
        snap = value;
        if (value != Snap::Off)
            lastMusicalSnap = value;
    }

    // Master snap switch, toggled by the piano roll's Q button and the panel-wide Q key. When off,
    // divisionBeats()/snapBeat() behave exactly like Snap::Off (no grid, raw beats pass through)
    // while `snap` keeps the chosen division, so toggling back on restores it. divisionBeatsRaw()
    // ignores this switch — that is what a one-shot "quantise now" action wants.
    bool snapEnabled = true;

    // The chosen division in beats regardless of snapEnabled (0.0 only for Snap::Off). beatsPerBar
    // is only consulted for Snap::Bar (pass TransportService's tsNum * 4 / tsDen). Factored out of
    // snapBeat() so a caller that needs the raw grid size — PianoRollComponent's quantise-now
    // action, which feeds TimelineDoc::quantiseNotes a gridBeats value rather than snapping a
    // single beat — doesn't duplicate this switch.
    double divisionBeatsRaw(double beatsPerBar) const noexcept {
        switch (snap) {
        case Snap::Off:
            return 0.0;
        case Snap::Bar:
            return beatsPerBar;
        case Snap::Whole:
            return 4.0;
        case Snap::Half:
            return 2.0;
        case Snap::Quarter:
            return 1.0;
        case Snap::Eighth:
            return 0.5;
        case Snap::Sixteenth:
            return 0.25;
        }
        return 0.0;
    }

    // The EFFECTIVE snap division (0.0 when snap is off — either Snap::Off or snapEnabled false).
    // Every magnetic edit and every grid paint reads this one.
    double divisionBeats(double beatsPerBar) const noexcept {
        return snapEnabled ? divisionBeatsRaw(beatsPerBar) : 0.0;
    }

    // Nearest multiple of the snap division; beatsPerBar is only consulted for Snap::Bar (pass
    // TransportService's tsNum * 4 / tsDen). Off passes `beat` through unchanged. Ties (exactly
    // halfway between two grid lines) round up (toward +infinity) — deterministic and simple;
    // beats are never negative in practice so this is also "away from zero".
    double snapBeat(double beat, double beatsPerBar) const noexcept {
        const double division = divisionBeats(beatsPerBar);
        if (division <= 0.0)
            return beat;
        return std::floor(beat / division + 0.5) * division;
    }
};

} // namespace synth::ui
