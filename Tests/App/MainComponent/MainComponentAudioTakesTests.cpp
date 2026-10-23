// Record with several armed Audio tracks: each records its own input, all at once, and the whole press commits
// as ONE undo step. Driven end to end through MainComponent with a fake 4-input device whose every input carries
// its own constant level, so each committed take can be checked for the channel it names.

#include "../../FakeAudioIODevice.h"
#include "../../Timeline/TakeFileTestHelpers.h"
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Modules/AudioInputModule.h"
#include "Modules/RecordTapModule.h"
#include "ProjectBundle.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <vector>

namespace {

using synth::test::FakeAudioIODevice;
using synth::test::readTakeWav;
using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

constexpr int kBlock = synth::test::kFakeDeviceBlockSize;
constexpr double kRate = synth::test::kFakeDeviceSampleRate;
constexpr int kRoundTrip = synth::test::kFakeDeviceInputLatency + synth::test::kFakeDeviceOutputLatency;

float levelOf(int input) { return 0.125f * (float)(input + 1); }

class MinimalProviderAT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "AudioTakesMock"; }
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

synth::AudioInputChoice deviceInput(int firstChannel, int numChannels) {
    synth::AudioInputChoice choice;
    choice.kind = synth::AudioInputChoice::Kind::Device;
    choice.firstChannel = firstChannel;
    choice.numChannels = numChannels;
    return choice; // no device name: whatever device is open
}

synth::AudioInputChoice wholeMix() {
    synth::AudioInputChoice choice;
    choice.kind = synth::AudioInputChoice::Kind::WholeMix;
    return choice;
}

} // namespace

class AudioTakesFlowTest : public ::testing::Test {
protected:
    void resetKeys() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1");
            s->setValue("aiPanelVisible", "0");
            s->setValue("minimapVisible", "1");
            s->setValue("bottomDockVisible", "0");
            s->setValue("timelineCountInBars", 0);
            s->saveIfNeeded();
        }
    }

    void SetUp() override {
        resetKeys();
        bundleDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("agentsynth_audio_takes_" + juce::Uuid().toString() + ".agsproj");
        bundleDir.deleteRecursively();
        mc = std::make_unique<MainComponent>(std::make_unique<MinimalProviderAT>());
        mc->setSize(1600, 900);
        mc->getAudioEngine().suspendDeviceCallback();
    }
    void TearDown() override {
        if (device != nullptr)
            mc->getAudioEngine().audioDeviceStopped();
        mc.reset();
        resetKeys();
        bundleDir.deleteRecursively();
    }

    // Audio Input -> Audio Output, so a Whole mix take hears the input through the master.
    void buildInputPatch() {
        auto& graph = mc->getAudioEngine().getGraph();
        mc->getGraphEditor().detachAllModuleComponents();
        graph.clear();
        graph.setPlayConfigDetails(4, 2, kRate, kBlock);
        auto in = graph.addNode(std::make_unique<AudioInputModule>());
        auto out = graph.addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
        for (int channel = 0; channel < 2; ++channel)
            graph.addConnection({{in->nodeID, channel}, {out->nodeID, channel}});
    }

    void startDevice(int inputs) {
        device = std::make_unique<FakeAudioIODevice>(inputs, 2);
        mc->getAudioEngine().audioDeviceAboutToStart(device.get());
        numInputs = inputs;
    }

    void driveBlocks(int blocks) {
        std::vector<std::vector<float>> in((std::size_t)numInputs);
        std::vector<const float*> inputs;
        for (int channel = 0; channel < numInputs; ++channel) {
            in[(std::size_t)channel].assign((std::size_t)kBlock, levelOf(channel));
            inputs.push_back(in[(std::size_t)channel].data());
        }
        std::vector<float> outL((std::size_t)kBlock), outR((std::size_t)kBlock);
        float* outputs[] = {outL.data(), outR.data()};
        for (int block = 0; block < blocks; ++block)
            mc->getAudioEngine().audioDeviceIOCallbackWithContext(inputs.data(), numInputs, outputs, 2, kBlock, {});
    }

    synth::TrackId armedAudioTrack(const juce::String& name, const synth::AudioInputChoice& input) {
        auto& doc = mc->getTimelineDoc();
        const auto track = doc.addTrack(synth::TrackKind::Audio, name);
        EXPECT_TRUE(doc.setTrackAudioInput(track, input));
        EXPECT_TRUE(doc.setTrackArmed(track, true));
        return track;
    }

    void pressRecord() { mc->getTimelinePanel().getTransportBar().getRecordButton().onClick(); }

    std::unique_ptr<MainComponent> mc;
    std::unique_ptr<FakeAudioIODevice> device;
    int numInputs = 0;
    juce::File bundleDir;
};

// Regression test for FRO1123: only the first armed audio track recorded, and it recorded the master mix.
TEST_F(AudioTakesFlowTest, TwoArmedTracksRecordTheirOwnInputsInOneUndoStep) {
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    buildInputPatch(); // no latent module, so the round trip is the device's alone
    startDevice(4);
    auto& doc = mc->getTimelineDoc();
    const auto vox = armedAudioTrack("Vox", deviceInput(1, 1));
    const auto gtr = armedAudioTrack("Gtr", deviceInput(2, 2));
    mc->getUndoManager().clearUndoHistory();

    pressRecord();
    ASSERT_TRUE(mc->isRecordingActiveForTest());
    driveBlocks(20);
    pressRecord();
    EXPECT_FALSE(mc->isRecordingActiveForTest());

    ASSERT_EQ(doc.getTrack(vox)->clips.size(), 1u);
    ASSERT_EQ(doc.getTrack(gtr)->clips.size(), 1u);
    const auto& voxClip = doc.getTrack(vox)->clips[0];
    const auto& gtrClip = doc.getTrack(gtr)->clips[0];
    EXPECT_NE(voxClip.assetRef, gtrClip.assetRef) << "two takes of one press never share a take number";
    EXPECT_TRUE(voxClip.assetRef == "Audio/take-1.wav" || voxClip.assetRef == "Audio/take-2.wav");

    // The input take keeps the full round-trip latency: what the player heard went through the graph.
    EXPECT_EQ((int)std::llround(voxClip.sourceStartSeconds * kRate), kRoundTrip);

    const auto voxWav = readTakeWav(bundleDir.getChildFile(voxClip.assetRef));
    ASSERT_TRUE(voxWav.ok);
    ASSERT_EQ(voxWav.numChannels, 1);
    EXPECT_GE(voxWav.lengthInSamples, 19 * kBlock);
    EXPECT_EQ(voxWav.audio.getSample(0, 100), levelOf(1)) << "Vox recorded input 2";
    EXPECT_EQ(voxWav.audio.getMagnitude(0, 0, (int)voxWav.lengthInSamples), levelOf(1));

    const auto gtrWav = readTakeWav(bundleDir.getChildFile(gtrClip.assetRef));
    ASSERT_TRUE(gtrWav.ok);
    ASSERT_EQ(gtrWav.numChannels, 2);
    EXPECT_EQ(gtrWav.audio.getSample(0, 100), levelOf(2)) << "Gtr left is input 3";
    EXPECT_EQ(gtrWav.audio.getSample(1, 100), levelOf(3)) << "Gtr right is input 4";

    mc->simulateUndoClick();
    EXPECT_TRUE(doc.getTrack(vox)->clips.empty()) << "one undo takes back the whole press";
    EXPECT_TRUE(doc.getTrack(gtr)->clips.empty());
}

TEST_F(AudioTakesFlowTest, NoInputRecordsNothingAndSaysWhy) {
    startDevice(2);
    auto& doc = mc->getTimelineDoc();
    const auto vox = armedAudioTrack("Vox", {});
    pressRecord();
    EXPECT_EQ(mc->getStatusBar().getTransientMessageForTest(), "Vox has no input - pick one in the side pane");
    EXPECT_TRUE(mc->getTimelinePanel().getTransportBar().isRecordingForTest()) << "Record still rolls";
    EXPECT_FALSE(mc->isRecordingActiveForTest()) << "but no take is in flight";
    driveBlocks(4);
    pressRecord();
    EXPECT_TRUE(doc.getTrack(vox)->clips.empty());
}

TEST_F(AudioTakesFlowTest, AnInputTheDeviceDoesNotHaveIsNotConnected) {
    startDevice(2);
    auto& doc = mc->getTimelineDoc();
    const auto vox = armedAudioTrack("Vox", deviceInput(3, 1));
    const auto keys = armedAudioTrack("Keys", deviceInput(0, 1));
    pressRecord();
    EXPECT_EQ(mc->getStatusBar().getTransientMessageForTest(), "Vox: its input is not connected");
    driveBlocks(4);
    pressRecord();
    EXPECT_TRUE(doc.getTrack(vox)->clips.empty());
    EXPECT_EQ(doc.getTrack(keys)->clips.size(), 1u) << "the connected track still records";
    for (const auto& clip : doc.getTrack(keys)->clips) {
        const auto file = synth::userSettingsRootDirectory().getChildFile(clip.assetRef);
        file.deleteFile();
        file.withFileExtension("agpk").deleteFile();
    }
}

TEST_F(AudioTakesFlowTest, WholeMixStillRecordsTheMasterAndOnlyOnce) {
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    buildInputPatch();
    startDevice(2);
    auto& doc = mc->getTimelineDoc();
    const auto mix = armedAudioTrack("Mix", wholeMix());
    const auto second = armedAudioTrack("Mix 2", wholeMix());
    mc->timerCallback(); // arms input monitoring, so the input reaches the master

    pressRecord();
    EXPECT_EQ(mc->getStatusBar().getTransientMessageForTest(),
              "Mix 2: the whole mix is already recording on another track");
    driveBlocks(10);
    pressRecord();

    ASSERT_EQ(doc.getTrack(mix)->clips.size(), 1u);
    EXPECT_TRUE(doc.getTrack(second)->clips.empty());
    const auto wav = readTakeWav(bundleDir.getChildFile(doc.getTrack(mix)->clips[0].assetRef));
    ASSERT_TRUE(wav.ok);
    EXPECT_EQ(wav.numChannels, RecordTapModule::kNumChannels);
    EXPECT_GT(wav.audio.getMagnitude(0, 0, (int)wav.lengthInSamples), 0.0f) << "the tap heard the master";
}
