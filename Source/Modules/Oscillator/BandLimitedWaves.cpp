// BandLimitedWaves.cpp -- the two residual tables behind the Oscillator's edged waveforms.
//
// Each waveform is its plain shape plus, around every edge, a correction: the difference between an ideal step (or
// slope change) and that step filtered by a Kaiser-windowed sinc. The correction reads only phase and phase increment,
// so a voice keeps no history: a restart, a pitch jump or a Pulse Width move has nothing to flush, and there is no
// latency.
//
// The filter was fitted to measurements at 48 kHz, against mip tables and oversampling. Half width 8 with cutoff 0.47
// of the sample rate and beta 8 leaves the 10-20 kHz band within 0.15 dB of an ideal saw and inharmonic energy below
// 16 kHz at -92 dB, the measuring floor. It deliberately lets harmonics between Nyquist and 2/3 of the sample rate
// through: they fold above 16 kHz, and even half width 16 leaves them at -35 dB on a C8 saw, so removing them would
// droop the top band instead. Half width 4 misses the alias mark (-77 dB). 256 steps per sample measures the same as
// 512 at half the cache.

#include "Modules/Oscillator/BandLimitedWaves.h"

#include <algorithm>
#include <vector>

namespace synth::bandlimited {

namespace {

constexpr double kKernelCutoff = 0.47; // fraction of the sample rate
constexpr double kKernelBeta = 8.0;
constexpr int kIntegrationStepsPerEntry = 4;

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 32; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

ResidualTables buildResidualTables() {
    constexpr double pi = 3.14159265358979323846;
    const int entries = kHalfWidthSamples * kStepsPerSample;
    const int fine = entries * kIntegrationStepsPerEntry;
    const double spacing = 1.0 / (kStepsPerSample * kIntegrationStepsPerEntry);

    std::vector<double> kernel((size_t)fine + 1);
    for (int i = 0; i <= fine; ++i) {
        const double t = i * spacing;
        const double x = 2.0 * kKernelCutoff * t;
        const double sinc = i == 0 ? 1.0 : std::sin(pi * x) / (pi * x);
        const double r = t / kHalfWidthSamples;
        kernel[(size_t)i] = sinc * besselI0(kKernelBeta * std::sqrt(std::max(0.0, 1.0 - r * r)));
    }
    double halfArea = 0.0;
    for (int i = 0; i < fine; ++i)
        halfArea += (kernel[(size_t)i] + kernel[(size_t)i + 1]) * 0.5 * spacing;

    // Integrated inwards from the far end, where both residuals are exactly 0. Scaling the half area to 1/2 makes the
    // step's correction -1/2 at the edge itself.
    ResidualTables tables;
    double step = 0.0, ramp = 0.0;
    for (int i = fine; i >= 0; --i) {
        if (i % kIntegrationStepsPerEntry == 0) {
            tables.step[(size_t)(i / kIntegrationStepsPerEntry)] = (float)step;
            tables.ramp[(size_t)(i / kIntegrationStepsPerEntry)] = (float)ramp;
        }
        if (i > 0) {
            const double stepBefore =
                step - (kernel[(size_t)i] + kernel[(size_t)i - 1]) * 0.5 * spacing * 0.5 / halfArea;
            ramp -= (step + stepBefore) * 0.5 * spacing;
            step = stepBefore;
        }
    }
    return tables;
}

} // namespace

const ResidualTables residualTables = buildResidualTables();

} // namespace synth::bandlimited
