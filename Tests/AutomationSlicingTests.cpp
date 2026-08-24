// Control-rate slicing (AudioEngine::setAutomationSlicingEnabled).
//
// With the flag on, renderNextBlock runs the WHOLE per-block sequence — transport tick, timeline
// snapshot open, MIDI capture, automation apply, graph render — once per 64-sample slice instead of
// once per callback, so automation moves 8x per 512-sample block instead of once.
//
// This file exists to keep three honest facts on the record:
//   1. slicing actually refines automation inside a block (otherwise the flag is pointless);
//   2. it is audio-neutral for time-invariant processing (the parity test);
//   3. it is NOT audio-neutral for anything that reads a control at block rate — which is exactly
//      why the flag ships OFF and has to be measured per patch before anyone turns it on.
// Plus a cost tripwire, which is a tripwire and not a benchmark: it prints both timings and only
// fails on an order-of-magnitude regression.
//
// Headless house rules as everywhere else: HostMode::Hosted, no audio device, no sleeps.
// 48 kHz, 512-sample blocks, 120 BPM => 24000 samples/beat, 8 slices per block.

#include "../Source/AudioEngine.h"
#include "../Source/Modules/FX/ChorusModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/MacroControlModule.h"
#include "../Source/Modules/ModuleBase.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/Transport/OfflineTransportDriver.h"
#include "TestAudioHelpers.h"
#include "Timeline/TimelineDoc.h"
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>

namespace {

using AudioGraphIOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
using clock_type = std::chrono::steady_clock;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kSliceSamples = 64; // must match AudioEngine::kAutomationSliceSamples
constexpr double kSamplesPerBeat = 24000.0;

void setParam(juce::AudioProcessor* processor, const juce::String& paramId, float denormalised) {
    auto* param = findParameterByID(processor, paramId);
    ASSERT_NE(param, nullptr) << "no parameter \"" << paramId << "\"";
    param->setValueNotifyingHost(param->convertTo0to1(denormalised));
}

double msSince(clock_type::time_point start) {
    return std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
}

bool isFinite(const juce::AudioBuffer<float>& buffer) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (!std::isfinite(data[i]))
                return false;
    }
    return true;
}

// Owns an engine plus its driver, in the order the driver's contract demands: build the graph
// first, construct the driver second (its ctor prepareForHost's the nodes just created).
struct Rig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;

    juce::AudioProcessorGraph& graph() { return engine.getGraph(); }

    void startEmptyGraph() {
        engine.initialise();
        graph().clear();
    }

    void finishGraph() { driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlockSize, 2); }

    ~Rig() {
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

} // namespace

// ============================================================================
// 0. The flag is off unless someone turns it on
// ============================================================================

TEST(AutomationSlicingTest, DefaultsOff) {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    EXPECT_FALSE(engine.isAutomationSlicingEnabled())
        << "slicing changes rendered audio for block-rate modules — it must never be on by default";

    engine.setAutomationSlicingEnabled(true);
    EXPECT_TRUE(engine.isAutomationSlicingEnabled());
    engine.setAutomationSlicingEnabled(false);
    EXPECT_FALSE(engine.isAutomationSlicingEnabled());
}

// ============================================================================
// 1. Slicing really does refine automation inside a block
// ============================================================================

TEST(AutomationSlicingTest, SlicingMovesAutomationWithinTheBlock) {
    // A ramp of 2000 units per beat. One 512-sample block is 512/24000 beats, so the LAST slice of
    // a block starts (512 - 64)/24000 beats after the block does: sliced automation must lead
    // unsliced automation by 2000 * 448/24000 == 37.33 units at the end of every block. That number
    // is the whole point of the feature, so it is asserted, not just observed.
    constexpr double kUnitsPerBeat = 2000.0;
    constexpr double kExpectedLead = kUnitsPerBeat * (kBlockSize - kSliceSamples) / kSamplesPerBeat;
    constexpr int kBlocks = 100;

    const auto renderAndReadCutoff = [](bool sliced) {
        Rig rig;
        rig.startEmptyGraph();

        auto out = rig.graph().addNode(std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioOutputNode));
        auto osc = rig.graph().addNode(std::make_unique<OscillatorModule>());
        auto filter = rig.graph().addNode(std::make_unique<FilterModule>());
        filter->properties.set("uuid", "slice000-0000-0000-0000-000000000001");
        if (auto* module = dynamic_cast<ModuleBase*>(filter->getProcessor()))
            module->setNodeUuid("slice000-0000-0000-0000-000000000001");
        rig.graph().addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
        rig.graph().addConnection({{filter->nodeID, 0}, {out->nodeID, 0}});
        rig.finishGraph();

        synth::TimelineDoc doc;
        const auto trackId = doc.addTrack(synth::TrackKind::Midi, "T");
        synth::AutomationLane::RangeSnapshot range;
        range.minValue = 20.0f;
        range.maxValue = 20000.0f;
        range.defaultValue = 440.0f;
        const auto laneId = doc.addLane(trackId, "slice000-0000-0000-0000-000000000001", "cutoff", range);
        doc.addBreakpoint(laneId, 0.0, 100.0);
        doc.addBreakpoint(laneId, 4.0, 100.0 + kUnitsPerBeat * 4.0);
        rig.engine.publishTimeline(doc);

        rig.engine.setAutomationSlicingEnabled(sliced);
        rig.driver->getTransport().play();
        rig.driver->renderBlocks(kBlocks);

        auto* cutoff = findParameterByID(filter->getProcessor(), "cutoff");
        return static_cast<double>(cutoff->convertFrom0to1(cutoff->getValue()));
    };

    const double unsliced = renderAndReadCutoff(false);
    const double sliced = renderAndReadCutoff(true);

    std::cout << "[ SLICING ] cutoff after " << kBlocks << " blocks: unsliced=" << unsliced << "  sliced=" << sliced
              << "  lead=" << (sliced - unsliced) << " (expected " << kExpectedLead << ")" << std::endl;

    EXPECT_NEAR(sliced - unsliced, kExpectedLead, 1.0)
        << "with slicing on, the parameter must track the LAST slice of the block, not its first sample";
}

// ============================================================================
// 2. Parity: time-invariant processing does not care about block size
// ============================================================================

TEST(AutomationSlicingTest, SliceParityTimeInvariantChain) {
    // Oscillator -> Filter -> VCA -> Audio Output, fixed parameters, no automation, and a Macro
    // bank supplying the VCA's CV (a DC source: the VCA multiplies by its CV channel, so without
    // one the chain renders silence). Every module here advances per SAMPLE, so the rendered audio
    // must not depend on how the samples are grouped into calls.
    const auto render = [](bool sliced) {
        Rig rig;
        rig.startEmptyGraph();

        auto out = rig.graph().addNode(std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioOutputNode));
        auto osc = rig.graph().addNode(std::make_unique<OscillatorModule>());
        auto filter = rig.graph().addNode(std::make_unique<FilterModule>());
        auto vca = rig.graph().addNode(std::make_unique<VCAModule>());
        auto macros = rig.graph().addNode(std::make_unique<MacroControlModule>());

        setParam(osc->getProcessor(), "level", 1.0f);
        setParam(filter->getProcessor(), "cutoff", 1200.0f);
        setParam(vca->getProcessor(), "gain", 0.8f);
        setParam(macros->getProcessor(), "macro1", 0.75f);

        rig.graph().addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
        rig.graph().addConnection({{filter->nodeID, 0}, {vca->nodeID, 0}});
        rig.graph().addConnection({{macros->nodeID, 0}, {vca->nodeID, 1}});
        rig.graph().addConnection({{vca->nodeID, 0}, {out->nodeID, 0}});
        rig.graph().addConnection({{vca->nodeID, 0}, {out->nodeID, 1}});
        rig.finishGraph();

        rig.engine.setAutomationSlicingEnabled(sliced);
        rig.driver->getTransport().play();
        return rig.driver->renderBlocks(64);
    };

    const auto unsliced = render(false);
    const auto sliced = render(true);

    ASSERT_EQ(unsliced.getNumSamples(), sliced.getNumSamples());
    EXPECT_TRUE(isFinite(unsliced));
    EXPECT_TRUE(isFinite(sliced));
    EXPECT_FALSE(TestAudioHelpers::isSilent(unsliced, 0, 1.0e-4f)) << "the parity chain must actually make sound";

    const float maxDiff = TestAudioHelpers::compareBuffers(unsliced, sliced, 0);
    std::cout << "[ SLICING ] time-invariant chain max-abs diff (off vs on) = " << maxDiff << std::endl;
    EXPECT_LT(maxDiff, 1.0e-3f) << "a per-sample chain must render the same audio whatever the slice size";
}

// ============================================================================
// 3. The documented reason the flag ships OFF: block-rate controls DO change
// ============================================================================

TEST(AutomationSlicingTest, SliceDifferenceOnBlockRateFx) {
    // ChorusModule reads its Rate CV ONCE per block (`cvRateVal = cvRate[0]`) and hands it to a
    // per-sample LFO inside juce::dsp::Chorus. Sampling a moving CV 8x more often therefore moves
    // the chorus LFO onto a different trajectory and keeps it there — a permanent, audible
    // difference in the rendered signal, with nothing wrong on either side.
    //
    // This test deliberately asserts NOTHING about equality. It asserts both renders are finite and
    // audible, and REPORTS the measured difference. That number is the evidence behind
    // AudioEngine::setAutomationSlicingEnabled defaulting to false: turning slicing on is an
    // audio-affecting decision for any patch containing a module like this, so it has to be a
    // measured, per-patch opt-in rather than a free optimisation.
    const auto render = [](bool sliced) {
        Rig rig;
        rig.startEmptyGraph();

        auto out = rig.graph().addNode(std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioOutputNode));
        auto osc = rig.graph().addNode(std::make_unique<OscillatorModule>());
        auto chorus = rig.graph().addNode(std::make_unique<ChorusModule>());
        auto lfo = rig.graph().addNode(std::make_unique<LFOModule>());

        setParam(osc->getProcessor(), "level", 1.0f);
        setParam(chorus->getProcessor(), "mix", 1.0f);
        setParam(chorus->getProcessor(), "depth", 1.0f);
        // Free-running Hz mode, unipolar: a CV that sweeps well inside one 512-sample block, and
        // whose own generation is per-sample (so the LFO itself is not what differs).
        setParam(lfo->getProcessor(), "mode", 0.0f);
        setParam(lfo->getProcessor(), "bipolar", 0.0f);
        setParam(lfo->getProcessor(), "rateHz", 8.0f);

        rig.graph().addConnection({{osc->nodeID, 0}, {chorus->nodeID, 0}});
        rig.graph().addConnection({{osc->nodeID, 0}, {chorus->nodeID, 1}});
        rig.graph().addConnection({{lfo->nodeID, 0}, {chorus->nodeID, 2}});
        rig.graph().addConnection({{chorus->nodeID, 0}, {out->nodeID, 0}});
        rig.graph().addConnection({{chorus->nodeID, 1}, {out->nodeID, 1}});
        rig.finishGraph();

        rig.engine.setAutomationSlicingEnabled(sliced);
        rig.driver->getTransport().play();
        return rig.driver->renderBlocks(128);
    };

    const auto unsliced = render(false);
    const auto sliced = render(true);

    ASSERT_EQ(unsliced.getNumSamples(), sliced.getNumSamples());
    EXPECT_TRUE(isFinite(unsliced));
    EXPECT_TRUE(isFinite(sliced));
    EXPECT_FALSE(TestAudioHelpers::isSilent(unsliced, 0, 1.0e-4f));
    EXPECT_FALSE(TestAudioHelpers::isSilent(sliced, 0, 1.0e-4f));

    const float maxDiff = TestAudioHelpers::compareBuffers(unsliced, sliced, 0);
    const float unslicedRms = TestAudioHelpers::computeRMS(unsliced, 0);
    const float slicedRms = TestAudioHelpers::computeRMS(sliced, 0);
    std::cout << "[ SLICING ] block-rate FX (Chorus with LFO on its Rate CV): max-abs diff = " << maxDiff
              << ", rms off = " << unslicedRms << ", rms on = " << slicedRms << "  <- this is why the flag defaults OFF"
              << std::endl;
}

// ============================================================================
// 4. Cost tripwire (NOT a benchmark)
// ============================================================================

TEST(AutomationSlicingTest, SlicingCostTripwire) {
    // Slicing multiplies the per-block overhead — graph traversal, playhead re-application,
    // transport tick, per-module block prologue — by blockSize/slice (8x here), while the
    // per-sample DSP work stays the same. This is NOT a benchmark: the numbers it prints depend on
    // whatever else the machine is doing, and the assertion is deliberately an order-of-magnitude
    // tripwire. It fires if slicing ever becomes catastrophically expensive (an allocation per
    // slice, an O(n^2) rebuild), not if it gets 20% slower.
    constexpr int kVoices = 16; // 16 * 3 modules + Audio Output = 49 nodes
    constexpr int kBlocks = 200;

    const auto timeRender = [](bool sliced) {
        Rig rig;
        rig.startEmptyGraph();

        auto out = rig.graph().addNode(std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioOutputNode));
        for (int i = 0; i < kVoices; ++i) {
            auto osc = rig.graph().addNode(std::make_unique<OscillatorModule>());
            auto filter = rig.graph().addNode(std::make_unique<FilterModule>());
            auto vca = rig.graph().addNode(std::make_unique<VCAModule>());
            setParam(osc->getProcessor(), "coarse", static_cast<float>(i % 12));
            setParam(filter->getProcessor(), "cutoff", 400.0f + 200.0f * static_cast<float>(i));
            rig.graph().addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
            rig.graph().addConnection({{filter->nodeID, 0}, {vca->nodeID, 0}});
            rig.graph().addConnection({{vca->nodeID, 0}, {out->nodeID, 0}});
            rig.graph().addConnection({{vca->nodeID, 0}, {out->nodeID, 1}});
        }
        EXPECT_EQ(rig.graph().getNumNodes(), kVoices * 3 + 1);
        rig.finishGraph();

        rig.engine.setAutomationSlicingEnabled(sliced);
        rig.driver->getTransport().play();

        rig.driver->renderBlocks(8); // warm-up: first-block allocations, cache, branch predictors
        const auto start = clock_type::now();
        auto rendered = rig.driver->renderBlocks(kBlocks);
        const double elapsedMs = msSince(start);

        EXPECT_TRUE(isFinite(rendered)) << (sliced ? "sliced" : "unsliced") << " render produced non-finite samples";
        return elapsedMs;
    };

    const double offMs = timeRender(false);
    const double onMs = timeRender(true);

    std::cout << "[ SLICING ] " << kBlocks << " blocks x " << (kVoices * 3 + 1) << " nodes: off = " << offMs
              << " ms, on = " << onMs << " ms, ratio = " << (onMs / std::max(offMs, 1.0e-3)) << "x" << std::endl;

    EXPECT_LT(onMs, 4.0 * std::max(offMs, 1.0)) << "slicing cost " << onMs << " ms vs " << offMs
                                                << " ms unsliced — something is scaling far worse than 8x overhead";
}
