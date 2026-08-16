#include "TakePlacement.h"

#include <algorithm>
#include <cmath>

namespace synth {

TakePlacement computeTakePlacement(const TakePlacementInput& input) noexcept {
    TakePlacement out;

    if (input.takeLengthSamples <= 0)
        return out; // nothing was recorded: no clip, no undo step

    // Defensive defaults rather than a division by zero: a take is always bounded by the transport
    // it was recorded against, and a nonsense rate/tempo means we no longer know what that was.
    const double sampleRate = input.sampleRate > 0.0 ? input.sampleRate : 44100.0;
    const double bpm = input.bpm > 0.0 ? input.bpm : 120.0;
    const double samplesPerBeat = 60.0 * sampleRate / bpm;

    const std::int64_t punchSample =
        std::max((std::int64_t)0, (std::int64_t)std::llround(input.punchInBeat * samplesPerBeat));

    if (!input.captureStartValid) {
        // No anchor: the tap never captured a block under a transport playhead. Fall back to the
        // pre-TL6-8 placement — the punch beat, untrimmed — which is the best answer available
        // without knowing where frame 0 actually sits.
        out.hasContent = true;
        out.clipStartSample = punchSample;
        out.trimFrames = 0;
        out.lengthFrames = input.takeLengthSamples;
    } else {
        // Where the take's frame 0 was PLAYED, as opposed to captured. See TakePlacement.h.
        const std::int64_t playedStartSample = input.captureStartTimelineSample +
                                               (std::int64_t)input.captureStartBlockOffset -
                                               (std::int64_t)input.recordingLatencySamples;

        out.clipStartSample = std::max({punchSample, playedStartSample, (std::int64_t)0});
        out.trimFrames = out.clipStartSample - playedStartSample;
        if (out.trimFrames >= input.takeLengthSamples)
            return out; // the take ended before its own punch: all pre-roll, nothing to commit

        out.lengthFrames = input.takeLengthSamples - out.trimFrames;
        out.hasContent = true;
    }

    out.clipStartBeat = (double)out.clipStartSample / samplesPerBeat;
    out.clipLengthBeats = std::max((double)out.lengthFrames / samplesPerBeat, input.minClipLengthBeats);
    out.sourceStartSeconds = (double)out.trimFrames / sampleRate;
    return out;
}

} // namespace synth
