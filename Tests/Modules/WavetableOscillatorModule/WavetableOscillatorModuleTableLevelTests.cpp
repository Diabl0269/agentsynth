// WavetableOscillatorModuleTableLevelTests.cpp
// The built-in tables sound equally loud: the same note at the same Level must not change level by more
// than a few dB when only the Table is switched.

#include "WavetableOscillatorModuleTestFixture.h"
#include <algorithm>

namespace {

constexpr float kPositions[] = {0.0f, 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1.0f};
constexpr float kMaxSpreadDb = 3.5f;
constexpr float kTargetSpreadPeakDb = 6.0f;

float toDb(float linear) { return 20.0f * std::log10(std::max(linear, 1.0e-9f)); }

struct TableLevel {
    float meanRmsDb = 0.0f; // RMS averaged in power across the scan positions
    float worstPeakDb = -100.0f;
};

TableLevel measureTable(int table) {
    double power = 0.0;
    float peak = 0.0f;
    for (const float position : kPositions) {
        WavetableOscillatorModule osc;
        osc.prepareToPlay(kSampleRate, kBlockSize);
        setChoice(osc, "table", table);
        setFloat(osc, "position", position);
        const auto out = render(osc, 13, 8);
        const float r = rms(out, 0);
        power += (double)r * r;
        peak = std::max(peak, out.getMagnitude(0, 0, out.getNumSamples()));
    }
    TableLevel level;
    level.meanRmsDb = toDb((float)std::sqrt(power / (double)std::size(kPositions)));
    level.worstPeakDb = toDb(peak);
    return level;
}

} // namespace

// Regression test for FRO858: Bell, Digital, Pulse and Basic Shapes rendered 12-20 dB quieter than the other
// tables at the same Level, so a song's chords vanished under its drums.
TEST(WavetableTableLevel, EveryBuiltInTableIsWithinAFewDbOfTheOthers) {
    float lo = 1000.0f, hi = -1000.0f, peakLo = 1000.0f, peakHi = -1000.0f;
    for (int table = 0; table < WavetableOscillatorModule::kNumBuiltIns; ++table) {
        const TableLevel level = measureTable(table);
        std::cout << "table " << table << ": rms " << level.meanRmsDb << " dB, peak " << level.worstPeakDb << " dB\n";
        lo = std::min(lo, level.meanRmsDb);
        hi = std::max(hi, level.meanRmsDb);
        peakLo = std::min(peakLo, level.worstPeakDb);
        peakHi = std::max(peakHi, level.worstPeakDb);
    }
    EXPECT_LE(hi - lo, kMaxSpreadDb) << "loudest table " << hi << " dB, quietest " << lo << " dB";
    EXPECT_LE(peakHi - peakLo, kTargetSpreadPeakDb) << "highest peak " << peakHi << " dB, lowest " << peakLo << " dB";
}
