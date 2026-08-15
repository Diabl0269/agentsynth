#pragma once

#include <algorithm>
#include <cmath>

// TimelineViewState — TL5-2: pure, headless-testable beat<->pixel mapping shared by the timeline
// ruler and the lanes grid (and, later, track content). No JUCE dependency and no component
// lifetime: TimelinePanelComponent owns the single instance and hands a reference to
// TimelineRulerComponent (getViewState() exposes it for later tasks), so every consumer maps
// beats to pixels identically and there is exactly one place zoom/scroll/snap state lives.
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

    // Snap division, expressed as a fraction of ONE BEAT (not of a whole note/bar) — the combo
    // item labelled "1" is Beat (division 1.0 beat); "1/2"/"1/4"/"1/8"/"1/16" are literally that
    // fraction of a beat (0.5 / 0.25 / 0.125 / 0.0625 beat). This matches TransportService's own
    // kMinLoopLengthBeats = 1/16 = 0.0625 — the smallest grid here is exactly that same unit, not
    // a musical "sixteenth note" measured against a whole note (which would be 0.25 beat).
    enum class Snap : int { Off = 0, Bar, Beat, Half, Quarter, Eighth, Sixteenth };
    Snap snap = Snap::Beat;

    // The current snap division, expressed as a fraction of one beat (0.0 for Snap::Off, meaning
    // "no grid"). beatsPerBar is only consulted for Snap::Bar (pass TransportService's
    // tsNum * 4 / tsDen). Factored out of snapBeat() (TL5-8) so a caller that needs the raw grid
    // size — PianoRollComponent::performQuantise, which feeds TimelineDoc::quantiseNotes a
    // gridBeats value rather than snapping a single beat — doesn't duplicate this switch.
    double divisionBeats(double beatsPerBar) const noexcept {
        switch (snap) {
        case Snap::Off:
            return 0.0;
        case Snap::Bar:
            return beatsPerBar;
        case Snap::Beat:
            return 1.0;
        case Snap::Half:
            return 0.5;
        case Snap::Quarter:
            return 0.25;
        case Snap::Eighth:
            return 0.125;
        case Snap::Sixteenth:
            return 0.0625;
        }
        return 0.0;
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
