#pragma once

#include "Modules/FX/ParametricEQModule.h"
#include "UI/ModuleViews/FrequencyGrid.h"
#include <vector>

// EqResponseCurve.h -- FRO16 (P9-10, docs/mixer.md §5.10): the mixer column thumbnail's own
// curve sampler. Same shape as EQCurveComponent's own recomputeMagnitudes(), reused rather than
// reimplemented (ParametricEQModule::responseDb is the one analytic source of truth for what the
// biquads actually realise -- EQCurveComponent.h's own header comment forbids a second
// approximation). Reads CURRENT parameter values only -- never audio -- so it is safe to call
// from the message thread with no processBlock in flight, and pure enough to unit-test without a
// juce::Component.
namespace synth::ui {

struct EqResponseCurve {
    static constexpr int kNumPoints = 48;
    static constexpr float kMinDb = -18.0f;
    static constexpr float kMaxDb = 18.0f;

    /** kNumPoints magnitude samples in dB, log-spaced 20Hz-20kHz, clamped to [kMinDb, kMaxDb].
     *  Snapshots `eq`'s band settings and output gain once, then samples responseDb() at each
     *  point -- the identical two-call sequence EQCurveComponent::recomputeMagnitudes() makes. */
    static std::vector<float> compute(const ParametricEQModule& eq) {
        const auto bands = eq.getBandSnapshots();
        const float outputGainDb = eq.getOutputGainDb();

        std::vector<float> magnitudes(kNumPoints, 0.0f);
        for (int i = 0; i < kNumPoints; ++i) {
            const float freq = FrequencyGrid::indexToFreq(i, kNumPoints);
            magnitudes[(size_t)i] =
                juce::jlimit(kMinDb, kMaxDb, ParametricEQModule::responseDb(bands, outputGainDb, freq));
        }
        return magnitudes;
    }
};

} // namespace synth::ui
