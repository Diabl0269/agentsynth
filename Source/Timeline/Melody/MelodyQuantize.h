// Turns cut notes into melody notes in clip-relative beats: pitch to the nearest semitone as sung, or snapped to a key
// with starts and ends snapped to a grid. Pure; no document types.
#pragma once

#include "Music/MusicalScale.h"
#include "Timeline/Melody/NoteCutter.h"

#include <optional>
#include <vector>

namespace synth::melody {

enum class MelodyMode { AsSung, SnapToKey };

struct MelodyOptions {
    MelodyMode mode = MelodyMode::AsSung;
    std::optional<MusicalScale> key; // SnapToKey only; empty means "detect it from the notes"
    double gridBeats = 0.25;         // SnapToKey only
    double secondsPerBeat = 0.5;
    double lengthBeats = 4.0; // notes are clamped to 0..lengthBeats
};

struct MelodyNote {
    double startBeat;
    double lengthBeats;
    int pitch;
    int velocity;
};

/** The key best fitting the notes' pitch classes, weighted by duration; chromatic when there are no notes. */
MusicalScale detectHumKey(const std::vector<CutNote>& notes);

/** The melody for `notes`. In SnapToKey mode with no key in `options`, `usedKey` (when given) receives the detected
 *  key; with a key it receives that key. In AsSung mode it is left alone. */
std::vector<MelodyNote> notesToMelody(const std::vector<CutNote>& notes, const MelodyOptions& options,
                                      MusicalScale* usedKey = nullptr);

} // namespace synth::melody
