#pragma once

#include "TimelineDoc.h"

#include <optional>
#include <vector>

namespace synth {

// What dragging a clip's edge does to the notes inside it. The order is the menu's and the cycle's order.
enum class ClipSizingMode {
    Trim,          // notes keep their beat on the timeline; the edge crops them
    CarryContents, // notes keep their place inside the clip, so a left edge carries them along
    Stretch,       // notes scale with the clip's length
};

enum class ClipEdge { Left, Right };

// One edge drag's outcome for a MIDI clip: where the clip lands and its notes, clip-relative and sorted.
struct ClipEdgeResult {
    double startBeat = 0.0;
    double lengthBeats = 0.0;
    std::vector<MidiNote> notes;
};

// The single rule for an edge drag to [newStartBeat, newStartBeat + newLengthBeats). The clip-lane preview
// and TimelineDoc::resizeClipEdge both call it, so what a drag shows is what the drop does. The span must be
// finite with a positive length; a Right edge keeps clip.startBeat whatever `newStartBeat` says.
ClipEdgeResult clipEdgeResult(const Clip& clip, ClipEdge edge, ClipSizingMode mode, double newStartBeat,
                              double newLengthBeats);

// What a Stretch edge drag of an audio clip to `newLengthBeats` reads from its asset: the clip's own window, and how
// many times faster to play it. nullopt for a MIDI clip, or a length or tempo that is not finite and positive.
struct AudioStretchWindow {
    double startSeconds = 0.0;
    double lengthSeconds = 0.0;
    double tempoRatio = 1.0;
};
std::optional<AudioStretchWindow> audioStretchWindow(const Clip& clip, double newLengthBeats, double secondsPerBeat);

// Points audio clip `id` at its stretched copy `stretchedRef` (read from 0 s) over [newStartBeat, + newLengthBeats),
// fades scaled alike. Call inside one recorded change; false (possibly part-applied) when a mutator refuses.
bool applyAudioClipStretch(TimelineDoc& doc, ClipId id, double newStartBeat, double newLengthBeats,
                           const juce::String& stretchedRef);

const char* clipSizingModeName(ClipSizingMode mode) noexcept;
const char* clipSizingModeDescription(ClipSizingMode mode) noexcept;
ClipSizingMode nextClipSizingMode(ClipSizingMode mode) noexcept;
// The settings-file spelling ("trim", "carry", "stretch"); anything else reads as nullopt.
const char* clipSizingModeKey(ClipSizingMode mode) noexcept;
std::optional<ClipSizingMode> clipSizingModeFromKey(const juce::String& key);

} // namespace synth
