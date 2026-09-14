#pragma once

// Table-synthesis primitives for the Wavetable oscillator (FRO73): the mip-pyramid geometry,
// the `Wavetable` storage type, and `TableBuilder`, which turns a harmonic spectrum into a
// band-limited mip pyramid via inverse FFT. Split out of WavetableOscillatorModule.h so that
// header could get under the file-size cap; WavetableOscillatorModule aliases these names back
// in (`using Wavetable = wavetable::Wavetable;` etc.) so its own API is unchanged.

#include <algorithm>
#include <array>
#include <cmath>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace wavetable {

// -------------------------------------------------------------------------
// Mip geometry
// -------------------------------------------------------------------------
constexpr int kFrameSize = 2048;   // samples per single-cycle frame (mip 0)
constexpr int kNumMips = 11;       // 1023 harmonics down to 1
constexpr int kMaxHarmonic = 1023; // highest harmonic mip 0 can hold

/** Samples stored for mip level m. Never drops below 64 so that linear interpolation
    of the stored frame stays well conditioned. */
constexpr int mipLength(int m) { return ((kFrameSize >> m) > 64) ? (kFrameSize >> m) : 64; }

/** Highest harmonic present in mip level m (also bounded by that mip's own Nyquist). */
constexpr int mipHarmonicLimit(int m) {
    const int byLevel = (kMaxHarmonic + 1) >> m; // 1024, 512, ... 1
    const int byNyquist = mipLength(m) / 2 - 1;
    return byLevel < byNyquist ? byLevel : byNyquist;
}

/** FFT order (log2 length) used to synthesise mip level m. */
constexpr int mipOrder(int m) {
    int order = 0;
    while ((1 << order) < mipLength(m))
        ++order;
    return order;
}

/** One wavetable: numFrames single-cycle frames, each stored as a mip pyramid.
    Immutable once built. */
struct Wavetable {
    int numFrames = 1;
    juce::String name;
    juce::String sourcePath; // empty for built-ins
    std::array<std::vector<float>, kNumMips> mips;

    const float* frameData(int mip, int frame) const {
        return mips[(size_t)mip].data() + (size_t)frame * (size_t)mipLength(mip);
    }
};

using TablePtr = std::shared_ptr<const Wavetable>;

// -------------------------------------------------------------------------
// Table synthesis (message thread only)
// -------------------------------------------------------------------------
/** Builds mip pyramids from harmonic spectra. Owns one FFT per distinct mip length and
    self-calibrates the inverse-transform gain, so the resulting tables do not depend on
    the platform FFT engine's normalisation convention. */
class TableBuilder {
public:
    TableBuilder();

    /** Allocates a table with room for numFrames frames at every mip level. */
    static std::unique_ptr<Wavetable> allocate(int numFrames, const juce::String& name, const juce::String& sourcePath);

    /** Renders one frame's whole mip pyramid from cosine/sine harmonic amplitudes.
        cosAmp/sinAmp are indexed by harmonic number (index 0 unused). */
    void renderFrame(Wavetable& wt, int frame, const float* cosAmp, const float* sinAmp, int maxHarmonic);

    /** Analyses one single-cycle frame of kFrameSize samples into harmonic amplitudes.
        Bin 0 (DC) is discarded so loaded tables cannot introduce a DC offset. */
    void analyseFrame(const float* cycle, float* cosAmp, float* sinAmp) {
        analyseCycle(cycle, kFrameSize, cosAmp, sinAmp);
    }

    /** Analyses a single cycle of `length` samples (a power of two in [64, kFrameSize]).

        Analysing at the source's own frame size — rather than resampling the cycle up to
        kFrameSize first — keeps the import exact: a 256-sample frame carries at most 127
        harmonics and they are read straight off a 256-point FFT, with none of the HF droop
        linear upsampling would introduce. The absolute FFT scale differs between sizes, but
        every frame of one import shares a size and normalise() rescales the table at the
        end, so only the relative amplitudes matter. */
    void analyseCycle(const float* cycle, int length, float* cosAmp, float* sinAmp);

    /** Collapses a spectrum onto sine phase, keeping magnitudes.

        This is the "spectral" import: every frame is resynthesised zero-phase, so scanning
        the stack cross-fades magnitudes instead of beating phase-incoherent frames against
        each other. It is what makes a table sampled from unrelated cycles morph smoothly. */
    static void collapseToSinePhase(float* cosAmp, float* sinAmp);

    /** Scales the whole table so mip 0 peaks at 1.0. Keeps relative frame levels. */
    static void normalise(Wavetable& wt);

private:
    static constexpr int kMinOrder = 6;  // shortest mip is 64 samples
    static constexpr int kMaxOrder = 11; // longest mip is kFrameSize samples

    /** Largest order whose length still fits in `length` (which callers clamp into range). */
    static int orderForLength(int length);

    juce::dsp::FFT& fftFor(int mip) { return fftByOrder(mipOrder(mip)); }
    juce::dsp::FFT& fftByOrder(int order) {
        return *ffts[(size_t)(juce::jlimit(kMinOrder, kMaxOrder, order) - kMinOrder)];
    }

    std::unique_ptr<juce::dsp::FFT> ffts[kMaxOrder - kMinOrder + 1];
    float invScale[kNumMips]{};
    std::vector<float> work;
};

} // namespace wavetable
