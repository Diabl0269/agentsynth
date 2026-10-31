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

// How the notes inside a clip move on screen while an undo or redo glides the clip from its old place to its restored
// one. The doc holds the restored notes at once; this says how to draw them back at their old place and bring them
// along.
enum class ClipNoteGlide {
    Stay,  // Trim (or no note moved): the notes keep their beat, the edge slides over them
    Shift, // Carry contents, or a pure move: the notes follow the clip's left edge
    Scale, // Stretch: each note is drawn through the gliding clip's own mapping, old span to new span
};

// A note's id and clip-relative start, taken at each sync so a later restore can tell which of the three it was.
struct ClipNoteStart {
    NoteId id;
    double startBeat = 0.0;
};
std::vector<ClipNoteStart> clipNoteStarts(const std::vector<MidiNote>& notes);

// How the notes moved when the clip went from [oldStart, + oldLength) with `before` to [newStart, + newLength) with
// `after`, matching notes by id: an unchanged length is a move (Shift); all relative starts unchanged is Carry (Shift);
// all of them scaled by the length ratio is Stretch (Scale); anything else keeps the notes on their beat (Stay).
ClipNoteGlide classifyClipNoteGlide(double oldLength, double newLength, const std::vector<ClipNoteStart>& before,
                                    const std::vector<ClipNoteStart>& after);

// Where a note's final x is drawn at the moment the clip's rectangle is `drawn` instead of its resting `rest`
// (both left and width, in pixels). Stay and Shift read only the left edge; Scale maps through both.
double glidedNoteX(ClipNoteGlide glide, double restingX, double restLeft, double restWidth, double drawnLeft,
                   double drawnWidth);

// What a Stretch edge drag of an audio clip to `newLengthBeats` reads and renders: the asset `sourceRef`, the window of
// it the clip shows, and how many times faster to play that window to fill the new length. With `useOrigin` and a
// stretch origin on the clip, the source is the original asset and the ratio is cumulative, so stretching again never
// starts from a stretched copy. `backToOriginal` is a cumulative ratio of exactly 1: the window of the original is the
// answer and nothing needs rendering. nullopt for a MIDI clip, or a length or tempo that is not finite and positive.
struct AudioStretchWindow {
    juce::String sourceRef;
    double startSeconds = 0.0;
    double lengthSeconds = 0.0;
    double tempoRatio = 1.0;
    bool backToOriginal = false;
    // What the clip records as its origin once it points at the render of this window; nullopt when backToOriginal.
    std::optional<Clip::StretchOrigin> origin;
};
std::optional<AudioStretchWindow> audioStretchWindow(const Clip& clip, double newLengthBeats, double secondsPerBeat,
                                                     bool useOrigin = true);

// Points audio clip `id` at `assetRef` (read from `sourceStartSeconds`) over [newStartBeat, + newLengthBeats), fades
// scaled alike, and records `origin` (nullopt clears it). The stretched copy is read from 0 s. Call inside one
// recorded change; false (possibly part-applied) when a mutator refuses.
bool applyAudioClipStretch(TimelineDoc& doc, ClipId id, double newStartBeat, double newLengthBeats,
                           const juce::String& assetRef, double sourceStartSeconds = 0.0,
                           const std::optional<Clip::StretchOrigin>& origin = std::nullopt);

const char* clipSizingModeName(ClipSizingMode mode) noexcept;
const char* clipSizingModeDescription(ClipSizingMode mode) noexcept;
ClipSizingMode nextClipSizingMode(ClipSizingMode mode) noexcept;
// The settings-file spelling ("trim", "carry", "stretch"); anything else reads as nullopt.
const char* clipSizingModeKey(ClipSizingMode mode) noexcept;
std::optional<ClipSizingMode> clipSizingModeFromKey(const juce::String& key);

} // namespace synth
