// PerformanceTests.cpp
//
// Wall-clock performance regression guards for the startup / preset-switch paths
// that the user reported as "stuck for quite some time". These tests MEASURE the
// real cost of the hot paths so a future change cannot silently reintroduce a hang:
//   - PresetLoadIsFast            : PresetManager::loadPreset per factory preset
//   - ModulationRoutingsIsFast    : getModulationRoutings/getModulationDisplayInfo (30fps UI)
//   - GraphEditorUpdateComponentsIsFast : GraphEditor::updateComponents on a preset switch
//
// Timings are printed with std::cout so the numbers are visible in the test log.

#include "../Source/AudioEngine.h"
#include "../Source/PresetManager.h"
#include "../Source/UI/GraphEditor.h"
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {
using clock_type = std::chrono::steady_clock;

double msSince(clock_type::time_point start) {
    return std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
}

constexpr int kNumPresets = 7; // Default..Poly Pad (indices 0..6)
constexpr int kPolyPadPreset = 6;
} // namespace

// Each factory preset must load quickly into a fresh graph. Real cost should be a
// few ms; a 250ms budget catches a genuine regression (e.g. a 100x blowup) without
// being flaky on a loaded CI box.
TEST(PerformanceTest, PresetLoadIsFast) {
    for (int i = 0; i < kNumPresets; ++i) {
        juce::AudioProcessorGraph graph;
        graph.setPlayConfigDetails(0, 2, 44100.0, 512);

        auto start = clock_type::now();
        bool loaded = gsynth::PresetManager::loadPreset(i, graph);
        double elapsedMs = msSince(start);

        EXPECT_TRUE(loaded) << "Preset " << i << " failed to load";
        std::cout << "[PERF] loadPreset(" << i << ") = " << elapsedMs << " ms" << std::endl;
        EXPECT_LT(elapsedMs, 250.0) << "Preset " << i << " load took " << elapsedMs << " ms (budget 250ms)";
    }
}

// getModulationRoutings()/getModulationDisplayInfo() run every UI frame (~30fps),
// so they must be cheap. Average per-call budget is 5ms; in practice these are
// sub-millisecond. We use Poly Pad (the heaviest factory preset) as the worst case.
TEST(PerformanceTest, ModulationRoutingsIsFast) {
    AudioEngine engine;
    engine.initialise();
    engine.getGraph().clear();
    bool loaded = gsynth::PresetManager::loadPreset(kPolyPadPreset, engine.getGraph());
    ASSERT_TRUE(loaded) << "Poly Pad preset (index 6) failed to load";

    constexpr int kIters = 100;

    // getModulationRoutings
    double maxRoutingsMs = 0.0;
    auto routingsStart = clock_type::now();
    for (int i = 0; i < kIters; ++i) {
        auto callStart = clock_type::now();
        auto routings = engine.getModulationRoutings();
        double callMs = msSince(callStart);
        maxRoutingsMs = std::max(maxRoutingsMs, callMs);
        (void)routings;
    }
    double avgRoutingsMs = msSince(routingsStart) / kIters;

    // getModulationDisplayInfo
    double maxDisplayMs = 0.0;
    auto displayStart = clock_type::now();
    for (int i = 0; i < kIters; ++i) {
        auto callStart = clock_type::now();
        auto info = engine.getModulationDisplayInfo();
        double callMs = msSince(callStart);
        maxDisplayMs = std::max(maxDisplayMs, callMs);
        (void)info;
    }
    double avgDisplayMs = msSince(displayStart) / kIters;

    std::cout << "[PERF] getModulationRoutings    avg=" << avgRoutingsMs << " ms  max=" << maxRoutingsMs << " ms"
              << std::endl;
    std::cout << "[PERF] getModulationDisplayInfo avg=" << avgDisplayMs << " ms  max=" << maxDisplayMs << " ms"
              << std::endl;

    EXPECT_LT(avgRoutingsMs, 5.0) << "getModulationRoutings avg " << avgRoutingsMs << " ms exceeds 5ms (runs at 30fps)";
    EXPECT_LT(avgDisplayMs, 5.0) << "getModulationDisplayInfo avg " << avgDisplayMs
                                 << " ms exceeds 5ms (runs at 30fps)";

    engine.shutdown();
}

// GraphEditor::updateComponents() rebuilds the visual graph on a preset switch.
// This is the headless analog of the user-facing "preset switch hang". We time it
// for the heaviest preset and again after switching to a different preset.
TEST(PerformanceTest, GraphEditorUpdateComponentsIsFast) {
    AudioEngine engine;
    engine.initialise();
    GraphEditor editor(engine);
    editor.setSize(1200, 800);

    // Load Poly Pad and time the first updateComponents (initial render).
    editor.detachAllModuleComponents();
    engine.getGraph().clear();
    ASSERT_TRUE(gsynth::PresetManager::loadPreset(kPolyPadPreset, engine.getGraph()))
        << "Poly Pad preset (index 6) failed to load";

    auto start1 = clock_type::now();
    editor.updateComponents();
    double firstMs = msSince(start1);
    std::cout << "[PERF] updateComponents (load Poly Pad) = " << firstMs << " ms" << std::endl;
    EXPECT_LT(firstMs, 500.0) << "updateComponents after loading Poly Pad took " << firstMs << " ms (budget 500ms)";

    // Switch to a different preset (index 0) and time updateComponents again —
    // this simulates a preset switch in the running app.
    editor.detachAllModuleComponents();
    engine.getGraph().clear();
    ASSERT_TRUE(gsynth::PresetManager::loadPreset(0, engine.getGraph())) << "Preset 0 failed to load";

    auto start2 = clock_type::now();
    editor.updateComponents();
    double secondMs = msSince(start2);
    std::cout << "[PERF] updateComponents (switch to preset 0) = " << secondMs << " ms" << std::endl;
    EXPECT_LT(secondMs, 500.0) << "updateComponents on preset switch took " << secondMs << " ms (budget 500ms)";

    editor.detachAllModuleComponents();
    engine.shutdown();
}
