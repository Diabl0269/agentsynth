#pragma once

#include "Modules/Lfo/LfoCustomWave.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <functional>
#include <vector>

namespace synth::ui {

// "Create custom LFO" on a lane range, as pure math: what the new LFO must be (rate, wave, level, phase offset), how
// the lane's own movement inside the range is flattened, and the new routing's amount lane, so that the base value plus
// the LFO reproduces the drawn curve. No GUI, no graph: the lane menu uses it to decide whether the item is enabled and
// the host uses it to execute, so the two can never disagree.
// docs/timeline/automation.md#create-custom-lfo-from-a-range

/** The longest range that can become an LFO cycle: the LFO's slowest rate, 8/1 (8 bars of 4/4). */
inline constexpr double kMaxCustomLfoBeats = 32.0;

enum class CustomLfoBlocker {
    None,
    NoRange,  // the range has no width
    TooLong,  // longer than the slowest LFO rate
    Flat,     // the lane does not move inside the range, so there is nothing to turn into an LFO
    LaneFull, // the edit would take the lane past its point cap
};

struct CustomLfoPlan {
    CustomLfoBlocker blocker = CustomLfoBlocker::NoRange;

    // The LFO: Custom shape, Sync, Retrig off, unipolar. The wave covers [0, rangeBeats / divisionBeats] of one cycle.
    int divisionIndex = 0; // index into synth::lfoRateDivisions()
    double divisionBeats = 0.0;
    synth::LfoCustomWave wave;
    double level = 0.0; // Level knob (a whole step of it): at least the drawn movement's height in the parameter's
                        // normalised range, which the wave's own height makes up exactly
    double phaseDegrees =
        0.0; // Phase offset, so the cycle starts at the range's first beat under the song-locked phase

    // The lane: breakpoints to take out and put in so the range plays flat at `baseValue` and everything outside plays
    // as before.
    double baseValue = 0.0; // the lowest value the drawn curve reaches, in the parameter's own units
    std::vector<double> removeBeats;
    std::vector<synth::AutomationLane::Breakpoint> addPoints;

    // The routing's amount lane: 0 outside the range, +1 inside.
    std::vector<synth::AutomationLane::Breakpoint> amountPoints;

    bool ok() const noexcept { return blocker == CustomLfoBlocker::None; }
};

/** Maps a parameter value (its own units) into the parameter's normalised 0..1 range. Must be monotonic. */
using ValueNormaliser = std::function<double(double)>;

/** Plans the conversion of `lane` over [startBeat, endBeat]; `blocker` says why it cannot be done, if so. */
CustomLfoPlan planCustomLfoFromRange(const synth::AutomationLane& lane, double startBeat, double endBeat,
                                     const ValueNormaliser& normalise);

/** The lane menu item's text for a blocker, "Create custom LFO (range is flat)" and so on; plain text when None. */
juce::String customLfoMenuText(CustomLfoBlocker blocker);

} // namespace synth::ui
