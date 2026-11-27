// Cuts a track of pitch frames into notes: runs of voiced frames, split where the sung pitch really moves to another
// note and not for vibrato or a scoop into the note. Pure DSP over PitchFrame; no document types.
#pragma once

#include "Timeline/Melody/PitchTracker.h"

#include <vector>

namespace synth::melody {

struct CutNote {
    double startSec;
    double endSec;
    float midi; // median pitch of the note's frames, fractional
    float rmsDb;
};

struct NoteCutterSettings {
    int medianFrames = 5;               // pitch median filter width, over voiced frames
    double bridgeGapSec = 0.030;        // an unvoiced gap this short does not end a note
    double splitSemitones = 0.7;        // pitch must stay this far from the note's centre to start a new note
    double glideSemitones = 0.25;       // a new note starts where the pitch first left the old centre by this much
    double splitHoldSec = 0.050;        // ... for this long
    double settleSec = 0.060;           // a note's first stretch (scoop, glide in) never counts as a departure
    double minNoteSec = 0.080;          // shorter notes are dropped
    double edgeCompensationSec = 0.015; // a window needs time to fill, so edges are found late/early by about this
};

/** The notes in `frames` (as from trackPitch), in time order. */
std::vector<CutNote> cutNotes(const std::vector<PitchFrame>& frames, const NoteCutterSettings& settings = {});

} // namespace synth::melody
