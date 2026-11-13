// ProjectOpenProfileTests.cpp
//
// A disabled profiling bench, not a test: saves a big project (PROFILE_TRACKS instrument tracks, default 90, plus one
// Sampler holding a real WAV and one Wavetable holding a real table) and prints how long each stage of opening it
// takes in a real MainComponent. Run by hand:
//   ./Tests --gtest_also_run_disabled_tests --gtest_filter='*ProjectOpenProfile*'

#include "../Mixer/ChannelFlow/ChannelFlowTestFixture.h"

#include "Modules/SamplerModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UI/Graph/ProjectLoad/LoadRevealAnimator.h"
#include "UI/Graph/ProjectLoad/ProjectLoadPipeline.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

// A stereo sine of `numSamples` at 48 kHz, written as a 24-bit WAV.
juce::File writeWav(const juce::File& file, int numSamples) {
    juce::AudioBuffer<float> buffer(2, numSamples);
    for (int i = 0; i < numSamples; ++i) {
        const float s = 0.5f * std::sin((float)i * 0.05f);
        buffer.setSample(0, i, s);
        buffer.setSample(1, i, -s);
    }
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(file), 48000.0, 2, 24, {}, 0));
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
    return file;
}

} // namespace

TEST_F(ChannelFlowTest, DISABLED_ProjectOpenProfile) {
    const int tracks = std::getenv("PROFILE_TRACKS") != nullptr ? std::atoi(std::getenv("PROFILE_TRACKS")) : 90;
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ProjectOpenBench");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto sampleFile = writeWav(dir.getChildFile("bench-sample.wav"), 60 * 48000);
    const auto tableFile = writeWav(dir.getChildFile("bench-table.wav"),
                                    WavetableOscillatorModule::kFrameSize * WavetableOscillatorModule::kMaxFrames);
    const auto bundle = dir.getChildFile("Bench.agsproj");

    int cards = 0;
    {
        MainComponent mc(std::make_unique<MockProviderCFT>());
        mc.setSize(1600, 1000);
        mc.getAudioEngine().suspendDeviceCallback();
        for (int i = 0; i < tracks; ++i)
            mc.getTimelinePanel().applyAddTrackMenuChoice(
                synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
        auto& editor = mc.getGraphEditor();
        editor.addModuleAtCanvasPosition("Sampler", {200, 200}, [&](juce::AudioProcessor& p) {
            static_cast<SamplerModule&>(p).loadSampleFile(sampleFile);
        });
        editor.addModuleAtCanvasPosition("Wavetable", {200, 600}, [&](juce::AudioProcessor& p) {
            static_cast<WavetableOscillatorModule&>(p).loadWavetableFile(tableFile);
        });
        cards = editor.getModuleComponents().size();
        ASSERT_TRUE(mc.saveProjectForTest(bundle));
    }

    double t0 = nowMs();
    SamplerModule sampler;
    sampler.loadSampleFile(sampleFile);
    const double sampleMs = nowMs() - t0;
    t0 = nowMs();
    WavetableOscillatorModule wavetable;
    wavetable.loadWavetableFile(tableFile);
    const double tableMs = nowMs() - t0;

    for (int run = 0; run < 3; ++run) {
        // Run 2 takes the on-screen path: the decodes leave the open, the reveal starts at its end.
        const bool onScreen = run == 2;
        MainComponent mc(std::make_unique<MockProviderCFT>());
        mc.setSize(1600, 1000);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.getProjectLoadPipeline().setForceInteractiveForTest(onScreen);
        t0 = nowMs();
        ASSERT_TRUE(mc.openProjectForTest(bundle));
        const double totalMs = nowMs() - t0;
        auto& editor = mc.getGraphEditor();
        if (onScreen) {
            std::printf("[open] on screen: open %.1f ms, %s, reveal lands at %.0f ms\n", totalMs,
                        mc.getProjectLoadPipeline().stageText().toRawUTF8(), editor.getLoadReveal().endMs());
            mc.getProjectLoadPipeline().assetLoads().finishAllForTest();
            editor.getLoadReveal().finish();
            continue;
        }
        t0 = nowMs();
        editor.detachAllModuleComponents();
        editor.updateComponents();
        const double uiMs = nowMs() - t0;
        std::printf("[open] %d tracks, %d cards, run %d: total %.1f ms | UI build alone %.1f | sample decode alone "
                    "%.1f, wavetable alone %.1f\n",
                    tracks, cards, run, totalMs, uiMs, sampleMs, tableMs);
    }
    dir.deleteRecursively();
}
