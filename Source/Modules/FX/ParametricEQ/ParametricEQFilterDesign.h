#pragma once

#include "Modules/FX/ParametricEQ/ParametricEQBandTypes.h"
#include <array>

// The Parametric EQ's band shapes as pure maths: the digital stages a band runs (RBJ biquads, cascaded
// Butterworth cuts, first-order stages) and the analog magnitude the curve draws. No module state; the band
// vocabulary (BandType, the slope list, kMaxStages) is in ParametricEQBandTypes.h.
namespace synth::eq {

inline constexpr float kButterworthQ = 0.70710678f;

/** One stage, normalised as juce::dsp::IIR::Coefficients stores it: {b0, b1, b2, a1, a2} over a0. */
using StageCoefs = std::array<float, 5>;
using BandStages = std::array<StageCoefs, kMaxStages>;

bool isCut(BandType type) noexcept;
/** Slope choice index for `slopeDbPerOct`, the nearest listed slope; the default for an unlisted value. */
int slopeIndexFor(int slopeDbPerOct) noexcept;
/** `index` clamped into kSlopesDbPerOct. */
int slopeDbFor(int index) noexcept;
/** Stages `type` runs at `slopeDbPerOct`: the cut's ceil(order / 2), otherwise 1. */
int stageCount(BandType type, int slopeDbPerOct) noexcept;

/** One RBJ stage for every type but the cuts' cascades (a cut here is its 12 dB/oct biquad, Tilt its
 *  first-order stage). `dest` holds 5 floats. Shelves and Peak are the four-band module's exact maths. */
void writeBiquad(BandType type, float centreHz, float gainDb, float q, double sampleRate, float* dest) noexcept;
/** Every stage of one band into `dest`; returns how many were written (1..kMaxStages). */
int designStages(BandType type, int slopeDbPerOct, float centreHz, float gainDb, float q, double sampleRate,
                 BandStages& dest) noexcept;
/** A pass-through stage. */
void writeUnity(float* dest) noexcept;

/** Analog magnitude in dB of one band at `freq`, every stage included; 0 for a non-finite or
 *  non-positive frequency. */
float bandMagnitudeDb(BandType type, float centreHz, float gainDb, float q, float freq,
                      int slopeDbPerOct = kSlopesDbPerOct[kDefaultSlopeIndex]) noexcept;

} // namespace synth::eq
