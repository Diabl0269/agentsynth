// WavetableOscillatorModuleTableLevelTests.cpp
// The built-in tables sound equally loud: the same note at the same Level must not change level by more
// than a few dB when only the Table is switched.

#include "WavetableOscillatorModuleTestFixture.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <vector>

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

namespace {

constexpr float kSweepPositions[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

struct Scenario {
    const char* name;
    std::vector<int> notes;
};

struct SceneLevel {
    float rmsDb = 0.0f;  // RMS averaged in power across the Position sweep
    float peakDb = 0.0f; // loudest sample across the sweep
};

/** One table, one pitch (or chord), through the module's real render path at the default Level and no filter. */
SceneLevel measureScenario(int table, const std::vector<int>& notes) {
    double power = 0.0;
    float peak = 0.0f;
    for (const float position : kSweepPositions) {
        WavetableOscillatorModule osc;
        osc.prepareToPlay(kSampleRate, kBlockSize);
        setChoice(osc, "table", table);
        setFloat(osc, "position", position);
        juce::AudioBuffer<float> block(kCh, kBlockSize);
        juce::MidiBuffer midi;
        for (const int note : notes)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
        block.clear();
        osc.processBlock(block, midi); // note-on block carries the smoothers' ramp-in, so it is discarded
        const auto out = render(osc, kCh, 16);
        const float r = rms(out, 0);
        power += (double)r * r;
        peak = std::max(peak, out.getMagnitude(0, 0, out.getNumSamples()));
    }
    return {toDb((float)std::sqrt(power / (double)std::size(kSweepPositions))), toDb(peak)};
}

} // namespace

// The mixer meter shows peak, the ear follows RMS. Prints peak/RMS per table at three pitches and a chord, and
// holds the RMS spread across tables to the same few dB at every one of them.
TEST(WavetableTableLevel, RmsSpreadStaysTightAtEveryPitchAndForAChord) {
    const Scenario scenarios[] = {{"C2", {36}}, {"C4", {60}}, {"C6", {84}}, {"C4+E4+G4", {60, 64, 67}}};
    constexpr int kScenarios = (int)std::size(scenarios);
    SceneLevel levels[WavetableOscillatorModule::kNumBuiltIns][kScenarios];
    for (int t = 0; t < WavetableOscillatorModule::kNumBuiltIns; ++t)
        for (int s = 0; s < kScenarios; ++s)
            levels[t][s] = measureScenario(t, scenarios[s].notes);

    std::cout << "table  (peak dB / RMS dB)";
    for (const auto& sc : scenarios)
        std::cout << "  " << sc.name;
    std::cout << "\n";
    for (int t = 0; t < WavetableOscillatorModule::kNumBuiltIns; ++t) {
        std::cout << "table " << t << ":";
        for (int s = 0; s < kScenarios; ++s)
            std::cout << "  " << std::fixed << std::setprecision(1) << levels[t][s].peakDb << " / "
                      << levels[t][s].rmsDb;
        std::cout << "\n";
    }
    for (int s = 0; s < kScenarios; ++s) {
        float lo = 1000.0f, hi = -1000.0f;
        for (int t = 0; t < WavetableOscillatorModule::kNumBuiltIns; ++t) {
            lo = std::min(lo, levels[t][s].rmsDb);
            hi = std::max(hi, levels[t][s].rmsDb);
        }
        std::cout << scenarios[s].name << " RMS spread " << (hi - lo) << " dB\n";
        EXPECT_LE(hi - lo, kMaxSpreadDb) << scenarios[s].name << ": loudest " << hi << " dB, quietest " << lo << " dB";
    }
}
