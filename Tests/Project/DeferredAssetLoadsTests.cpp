// DeferredAssetLoadsTests.cpp -- a sample or wavetable restored inside an on-screen project open decodes off the
// message thread and lands through the module's own install path; outside one nothing changes (DeferredAssetLoads.h).

#include "Modules/ModuleFileStateKeys.h"
#include "Modules/SamplerModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "Project/DeferredAssetLoads.h"
#include <cmath>
#include <gtest/gtest.h>

namespace {

struct TempWav {
    juce::File file;
    explicit TempWav(int numSamples) {
        file = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("deferred", ".wav");
        juce::AudioBuffer<float> buffer(1, numSamples);
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample(0, i, 0.5f * std::sin((float)i * 0.1f));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(new juce::FileOutputStream(file), 48000.0, 1, 16, {}, 0));
        writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
    }
    ~TempWav() { file.deleteFile(); }
};

juce::var stateFor(const char* key, const juce::File& file) {
    juce::DynamicObject::Ptr state = new juce::DynamicObject();
    state->setProperty(key, file.getFullPathName());
    return juce::var(state.get());
}

} // namespace

TEST(DeferredAssetLoads, OutsideAScopeASampleLoadsAtOnce) {
    TempWav wav(4800);
    SamplerModule sampler;
    ASSERT_EQ(synth::DeferredAssetLoads::active(), nullptr);
    sampler.setExtraState(stateFor(synth::module_file_keys::kSampleFile, wav.file));
    EXPECT_FALSE(sampler.isSampleLoadPending());
    EXPECT_EQ(sampler.getSampleFilePath(), wav.file.getFullPathName());
}

TEST(DeferredAssetLoads, InsideAScopeASampleDecodesLaterAndKeepsItsPath) {
    TempWav wav(4800);
    synth::DeferredAssetLoads loads;
    loads.holdDecodesForTest(true);
    SamplerModule sampler;
    {
        const synth::DeferredAssetLoads::Scope scope(&loads);
        sampler.setExtraState(stateFor(synth::module_file_keys::kSampleFile, wav.file));
    }
    EXPECT_TRUE(sampler.isSampleLoadPending());
    EXPECT_TRUE(sampler.getSampleFilePath().isEmpty()) << "not installed yet";
    EXPECT_EQ(sampler.getExtraState()[synth::module_file_keys::kSampleFile].toString(), wav.file.getFullPathName())
        << "a save or undo snapshot taken meanwhile keeps the file";
    EXPECT_EQ(loads.counts(synth::DeferredAssetLoads::Kind::Sample).total, 1);
    EXPECT_EQ(loads.counts(synth::DeferredAssetLoads::Kind::Sample).done, 0);
    ASSERT_EQ(loads.pendingOwners().size(), 1u);

    const void* landed = nullptr;
    loads.onInstalled = [&](const void* owner) { landed = owner; };
    loads.finishAllForTest();
    EXPECT_EQ(landed, &sampler);
    EXPECT_FALSE(sampler.isSampleLoadPending());
    EXPECT_EQ(sampler.getSampleFilePath(), wav.file.getFullPathName());
    EXPECT_NE(sampler.getSample(), nullptr);
    EXPECT_EQ(loads.counts(synth::DeferredAssetLoads::Kind::Sample).done, 1);
    EXPECT_FALSE(loads.hasPending());
}

TEST(DeferredAssetLoads, ALaterLoadWinsOverAPendingDecode) {
    TempWav first(4800), second(9600);
    synth::DeferredAssetLoads loads;
    loads.holdDecodesForTest(true);
    SamplerModule sampler;
    {
        const synth::DeferredAssetLoads::Scope scope(&loads);
        sampler.setExtraState(stateFor(synth::module_file_keys::kSampleFile, first.file));
    }
    ASSERT_TRUE(sampler.loadSampleFile(second.file)); // the user picks another file before the first lands
    loads.finishAllForTest();
    EXPECT_EQ(sampler.getSampleFilePath(), second.file.getFullPathName());
    EXPECT_EQ(loads.counts(synth::DeferredAssetLoads::Kind::Sample).done, 1) << "the item still counts as in";
}

TEST(DeferredAssetLoads, AModuleGoneBeforeItsDecodeLandsIsSkipped) {
    TempWav wav(4800);
    synth::DeferredAssetLoads loads;
    loads.holdDecodesForTest(true);
    {
        auto sampler = std::make_unique<SamplerModule>();
        const synth::DeferredAssetLoads::Scope scope(&loads);
        sampler->setExtraState(stateFor(synth::module_file_keys::kSampleFile, wav.file));
    }
    loads.finishAllForTest(); // must not touch the freed module
    EXPECT_FALSE(loads.hasPending());
}

TEST(DeferredAssetLoads, AResetDropsDecodesStillInFlight) {
    TempWav wav(4800);
    synth::DeferredAssetLoads loads;
    loads.holdDecodesForTest(true);
    SamplerModule sampler;
    {
        const synth::DeferredAssetLoads::Scope scope(&loads);
        sampler.setExtraState(stateFor(synth::module_file_keys::kSampleFile, wav.file));
    }
    loads.reset(); // a newer open replaced the document
    loads.finishAllForTest();
    EXPECT_TRUE(sampler.getSampleFilePath().isEmpty());
    EXPECT_EQ(loads.counts(synth::DeferredAssetLoads::Kind::Sample).total, 0);
}

TEST(DeferredAssetLoads, AWavetableDecodesLaterAndReportsItsFileMeanwhile) {
    TempWav wav(WavetableOscillatorModule::kFrameSize * 4);
    synth::DeferredAssetLoads loads;
    loads.holdDecodesForTest(true);
    WavetableOscillatorModule wavetable;
    {
        const synth::DeferredAssetLoads::Scope scope(&loads);
        wavetable.setExtraState(stateFor(synth::module_file_keys::kWavetableFile, wav.file));
    }
    EXPECT_FALSE(wavetable.hasLoadedWavetable());
    EXPECT_EQ(wavetable.getWavetableFile(), wav.file);
    EXPECT_EQ(loads.counts(synth::DeferredAssetLoads::Kind::Wavetable).total, 1);
    loads.finishAllForTest();
    EXPECT_TRUE(wavetable.hasLoadedWavetable());
    EXPECT_EQ(wavetable.getWavetableFile(), wav.file);
}
