#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <cmath>
#include <juce_core/juce_core.h>

// ClipAccessibilityText.h: what a screen reader says for the timeline's keyboard clip, e.g.
// "MIDI clip Bassline, track Bass, bars 5 to 9". Pure, so the wording is unit-testable without
// a component or a native accessibility peer.

namespace synth::ui {

/** A 1-based bar position for `beat`: "5" on a downbeat, "5 beat 3" inside the bar. */
inline juce::String describeBarPosition(double beat, double beatsPerBar) {
    const double bpb = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    const double bars = std::max(0.0, beat) / bpb;
    const int bar = (int)std::floor(bars + 1e-9);
    const double beatInBar = std::max(0.0, std::max(0.0, beat) - (double)bar * bpb) + 1.0;
    if (std::abs(beatInBar - 1.0) < 1e-6)
        return juce::String(bar + 1);
    return juce::String(bar + 1) + " beat " +
           juce::String(beatInBar, 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
}

/** The spoken description of `clip` on `track`: kind, name, track and bar span. A clip ending on
 *  a bar line reads "bars 5 to 9" (the end is the boundary where the next bar begins); a muted
 *  clip adds ", muted". */
inline juce::String describeClipForAccessibility(const synth::Clip& clip, const synth::Track& track,
                                                 double beatsPerBar) {
    const double bpb = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    const auto start = describeBarPosition(clip.startBeat, bpb);
    const auto end = describeBarPosition(clip.startBeat + clip.lengthBeats, bpb);
    const bool wholeBars = !start.contains("beat") && !end.contains("beat");
    juce::String text = juce::String(clip.assetRef.isEmpty() ? "MIDI clip " : "Audio clip ") + clip.name + ", track " +
                        track.name + ", ";
    text += wholeBars ? "bars " + start + " to " + end : "from bar " + start + " to bar " + end;
    if (clip.muted)
        text += ", muted";
    return text;
}

} // namespace synth::ui
