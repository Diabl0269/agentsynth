// LfoCustomWave.h -- pure data + edit model for LFOModule's Custom waveform: a small
// breakpoint list (shaped exactly like CurveModel's, but headless -- Core has no juce_gui_basics
// dependency), its JSON round-trip (LFOModule::getExtraState/setExtraState, trusted path only --
// see docs/ai/patch-safety.md), and the 1024-entry lookup table the audio thread reads. No JUCE
// GUI, no locks, no allocation outside renderTable's own fixed-size out-param.
#pragma once

#include "Modules/Envelope/EnvelopeGenerator.h"
#include <array>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/** A custom LFO waveform: an ordered list of (x, y, bend) breakpoints, x in [0, 1) phase space,
 *  y in [0, 1] (bipolar/unipolar mapping happens in LFOModule, not here), bend the OUTGOING
 *  segment's shape amount (`EnvelopeGenerator::shape`'s `c`) -- the last point's bend is always 0
 *  (no outgoing segment). Always sanitised to at least `kMinPoints` points with front.x == 0 and
 *  back.x == 1 -- see `sanitise()`'s doc comment for the exact rules a caller can rely on.
 */
struct LfoCustomWave {
    struct Point {
        float x = 0.0f;
        float y = 0.0f;
        float bend = 0.0f; // outgoing segment's bend; meaningless (and always 0) on the last point
    };

    static constexpr int kVersion = 1;
    static constexpr int kMinPoints = 2;
    static constexpr int kMaxPoints = 64;
    static constexpr int kTableSize = 1024;
    /** `renderTable`'s output: index kTableSize duplicates index 0 (wrap guard), so linear
     *  interpolation across the phase=1 seam never needs a modulo. */
    using Table = std::array<float, kTableSize + 1>;

    std::vector<Point> points;

    /** The Triangle preset -- what a freshly-appended Custom shape starts as, and what `Tool::
     *  Clear`'s undo history returns to via `preset(Preset::Triangle)` re-application if wanted.
     *  Not the same as `Tool::Clear` itself, which flattens to a single mid-level segment. */
    static LfoCustomWave defaultWave();

    enum class Preset { Triangle, RampUp, RampDown, Square, Pulse, Steps4, SoftSine };
    /** Always returns a valid (already-sanitised-shape) wave -- see LfoCustomWaveTests.
     *  PresetsAreAllValidAndDistinct. */
    static LfoCustomWave preset(Preset);

    enum class Tool { Invert, Reverse, Straighten, Clear };
    /** Whole-wave edit, one undo step's worth of change. See LfoCustomWaveTests for the exact
     *  per-tool point/bend transform. */
    void apply(Tool);

    bool operator==(const LfoCustomWave&) const;
    /** True iff this wave is exactly `preset(Preset::Triangle)` -- what `getExtraState()` uses to
     *  decide whether to emit anything at all (LFOModule.h). */
    bool isDefault() const;

    /** `{"version":1,"points":[{"x":..,"y":..,"bend":..}, ...]}`. Always well-formed for a
     *  sanitised wave (every instance reachable outside `fromVar` mid-parse is). */
    juce::var toVar() const;

    /** Never fails: garbage input (wrong shape, wrong version, too few valid points, ...)
     *  resolves to `defaultWave()`. See `sanitise()`'s doc comment for the exact rules -- this is
     *  the ONLY entry point untrusted-shaped JSON should ever reach (LFOModule::setExtraState is
     *  trusted-path only regardless; this is defence in depth, not the security boundary). */
    static LfoCustomWave fromVar(const juce::var&);

    /** Clamps/sorts/pins this wave into the invariants every other member relies on: x, y in
     *  [0, 1]; bend in [-1, 1]; points sorted by x and capped at `kMaxPoints`; front.x == 0,
     *  back.x == 1, back.bend == 0; falls back to `defaultWave()` if fewer than `kMinPoints`
     *  points survive. Called by `fromVar` and by `LFOModule::setCustomWave` -- a caller building
     *  a wave from the graph editor never needs to call this directly, but it is safe (and a
     *  no-op past the first pass) if it does. */
    void sanitise();

    /** `phase` in [0, 1) -> y in [0, 1], via the segment containing it and `EnvelopeGenerator::
     *  shape`. Right-continuous at a zero-length (step) segment: `evaluate(x)` for `x` exactly at
     *  a step's x reads the segment STARTING there, not the one ending there. */
    float evaluate(float phase) const;

    /** `out[k] = evaluate(k / kTableSize)` for `k` in `[0, kTableSize)`; `out[kTableSize] =
     *  out[0]` (the wrap guard `Table`'s own comment describes). No allocation. */
    void renderTable(Table& out) const;
};

} // namespace synth
