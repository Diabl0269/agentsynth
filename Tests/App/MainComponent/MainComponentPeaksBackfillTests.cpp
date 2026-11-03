// A project saved before imports wrote their waveform sidecar has audio clips with no waveform file. Opening it writes
// each missing sidecar once, off the message thread, so the waveform appears, and leaves the project clean
// (docs/timeline/clips.md, "Waveform sidecar backfill").

#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Timeline/PeaksFile.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <cmath>
#include <gtest/gtest.h>

namespace {

class NoopProviderPB : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "PeaksBackfillMock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"mock-model"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        if (callback)
            callback(AIResponse{true, "Mock response.", {}, {}});
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "mock-model";
    int requestTimeoutMs = 240000;
};

void writeSineWav(const juce::File& file, double seconds) {
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> out(file.createOutputStream());
    ASSERT_NE(out, nullptr);
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(out.get(), 44100.0, 1, 16, {}, 0));
    ASSERT_NE(writer, nullptr);
    out.release();
    juce::AudioBuffer<float> buffer(1, (int)(44100.0 * seconds));
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.5f * std::sin(0.05f * (float)i));
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

void pumpMessages(int times) {
    for (int i = 0; i < times; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

} // namespace

class PeaksBackfillTest : public ::testing::Test {
protected:
    void SetUp() override {
        bundleDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("agentsynth_peaks_backfill_" + juce::Uuid().toString() + ".agsproj");
        bundleDir.deleteRecursively();
        mc = std::make_unique<MainComponent>(std::make_unique<NoopProviderPB>());
        mc->setSize(1600, 900);
        mc->getAudioEngine().suspendDeviceCallback();
    }
    void TearDown() override {
        mc.reset();
        bundleDir.deleteRecursively();
    }

    // A saved project with one audio clip on `assetRef`'s wav, reopened after `Peaks/<stem>.agpk` is removed.
    void saveWithAudioClip(const juce::String& stem) {
        ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
        auto& doc = mc->getTimelineDoc();
        const auto track = doc.addTrack(synth::TrackKind::Audio, "Imported");
        const auto clip = doc.addClip(track, 0.0, 8.0, stem);
        writeSineWav(bundleDir.getChildFile("Audio/" + stem + ".wav"), 2.0);
        ASSERT_TRUE(doc.setClipAsset(clip, "Audio/" + stem + ".wav", 0.0));
        ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
        bundleDir.getChildFile("Peaks/" + stem + ".agpk").deleteFile();
    }

    std::unique_ptr<MainComponent> mc;
    juce::File bundleDir;
};

TEST_F(PeaksBackfillTest, OpeningAProjectWritesTheMissingSidecarAndTheWaveformAppears) {
    saveWithAudioClip("old");
    const auto peaksFile = bundleDir.getChildFile("Peaks/old.agpk");
    ASSERT_FALSE(peaksFile.existsAsFile());

    ASSERT_TRUE(mc->openProjectForTest(bundleDir));
    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
    while (!peaksFile.existsAsFile() && juce::Time::getMillisecondCounter() < deadline)
        pumpMessages(1);
    pumpMessages(20); // the cache reset lands after the file

    synth::PeaksFile::Data peaks;
    ASSERT_TRUE(synth::PeaksFile::read(peaksFile, peaks)) << "the sidecar was written";
    EXPECT_FALSE(peaks.buckets.empty());
    const auto opened = mc->getTimelineDoc().getTracks().back().clips.at(0).id;
    EXPECT_NE(mc->getTimelinePanel().getClipLaneArea().getWaveformShapeForTest(opened), "none");
    EXPECT_FALSE(mc->isProjectDirty()) << "writing a sidecar is not an edit";
}

TEST_F(PeaksBackfillTest, AnExistingSidecarIsLeftAlone) {
    saveWithAudioClip("kept");
    const auto peaksFile = bundleDir.getChildFile("Peaks/kept.agpk");
    peaksFile.getParentDirectory().createDirectory();
    ASSERT_TRUE(peaksFile.replaceWithText("marker"));

    ASSERT_TRUE(mc->openProjectForTest(bundleDir));
    pumpMessages(30);
    EXPECT_EQ(peaksFile.loadFileAsString(), "marker");
}
