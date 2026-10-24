// The engine's input recorder: takes recorded straight from the device (or host) input, beside the graph. Driven
// through a Hosted engine (prepareForHost + processHostBlock, no hardware) with a distinct signal on every input
// channel, so each take can be checked sample for sample against the channel it names. Plus the device-input
// channel map, the input level peaks, the rate-change halt and the "turn inputs on" device setup.

#include "../FakeAudioIODevice.h"
#include "../Timeline/TakeFileTestHelpers.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/DeviceInputEnable.h"
#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace {

using synth::test::readTakeWav;
using synth::test::ScopedTakeFile;

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

// A different, exactly representable value per (channel, frame), so a take holding the wrong channel or a frame
// off by one cannot pass.
float inputSample(int channel, juce::int64 frame) {
    return (float)(channel + 1) / 16.0f + (float)(frame % 256) / 8192.0f;
}

struct HostedRig {
    explicit HostedRig(int inputs, bool sliced = false)
        : numInputs(inputs) {
        engine.setAutomationSlicingEnabled(sliced);
        engine.prepareForHost(kRate, kBlock, numInputs, 2);
    }
    ~HostedRig() { engine.releaseFromHost(); }

    // One host block; the host's input is the low channels of the one in/out buffer.
    void render() {
        juce::AudioBuffer<float> buffer(std::max(numInputs, 2), kBlock);
        buffer.clear();
        for (int channel = 0; channel < numInputs; ++channel)
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample(channel, i, inputSample(channel, rendered + i));
        juce::MidiBuffer midi;
        engine.processHostBlock(buffer, midi);
        rendered += kBlock;
    }

    AudioEngine engine{AudioEngine::HostMode::Hosted};
    int numInputs;
    juce::int64 rendered = 0;
};

void expectTakeHoldsChannels(const juce::File& wav, std::initializer_list<int> channels, juce::int64 frames) {
    const auto contents = readTakeWav(wav);
    ASSERT_TRUE(contents.ok);
    ASSERT_EQ(contents.numChannels, (int)channels.size());
    ASSERT_EQ(contents.lengthInSamples, frames);
    int column = 0;
    for (const int channel : channels) {
        for (juce::int64 i = 0; i < frames; ++i)
            ASSERT_EQ(contents.audio.getSample(column, (int)i), inputSample(channel, i))
                << "input " << channel << " frame " << i;
        ++column;
    }
}

} // namespace

TEST(InputTakeRecorderTest, MonoSlotRecordsExactlyItsChannelAnchoredAtTheTransport) {
    HostedRig rig(4);
    ASSERT_TRUE(rig.engine.getTransport().locateBeat(2.0));
    ASSERT_TRUE(rig.engine.getTransport().play());

    ScopedTakeFile wav("agentsynth_input_mono.wav");
    ScopedTakeFile peaks("agentsynth_input_mono.agpk");
    auto& recorder = rig.engine.getInputRecorder();
    const int slot = recorder.startSlot(1, 1, wav.file, peaks.file, kRate);
    ASSERT_GE(slot, 0);
    for (int block = 0; block < 6; ++block)
        rig.render();
    const auto result = recorder.stopSlot(slot);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.lengthSamples, 6 * kBlock);
    ASSERT_TRUE(result.captureStartValid);
    EXPECT_EQ(result.captureStartTimelineSample, 48000) << "beat 2 at 120 bpm, 48 kHz";
    expectTakeHoldsChannels(wav.file, {1}, 6 * kBlock);
}

TEST(InputTakeRecorderTest, StereoPairAndAMonoSlotRecordAtOnce) {
    HostedRig rig(4);
    ScopedTakeFile wavA("agentsynth_input_pair.wav");
    ScopedTakeFile peaksA("agentsynth_input_pair.agpk");
    ScopedTakeFile wavB("agentsynth_input_first.wav");
    ScopedTakeFile peaksB("agentsynth_input_first.agpk");
    auto& recorder = rig.engine.getInputRecorder();
    const int pair = recorder.startSlot(2, 2, wavA.file, peaksA.file, kRate);
    const int first = recorder.startSlot(0, 1, wavB.file, peaksB.file, kRate);
    ASSERT_GE(pair, 0);
    ASSERT_GE(first, 0);
    ASSERT_NE(pair, first);
    EXPECT_TRUE(recorder.isAnyCapturing());
    for (int block = 0; block < 3; ++block)
        rig.render();
    ASSERT_TRUE(recorder.stopSlot(pair).ok);
    ASSERT_TRUE(recorder.stopSlot(first).ok);
    EXPECT_FALSE(recorder.isAnyCapturing());
    expectTakeHoldsChannels(wavA.file, {2, 3}, 3 * kBlock);
    expectTakeHoldsChannels(wavB.file, {0}, 3 * kBlock);
}

TEST(InputTakeRecorderTest, SlicedRenderPassesKeepEveryFrameInOrder) {
    HostedRig rig(2, /*sliced*/ true);
    ScopedTakeFile wav("agentsynth_input_sliced.wav");
    ScopedTakeFile peaks("agentsynth_input_sliced.agpk");
    auto& recorder = rig.engine.getInputRecorder();
    const int slot = recorder.startSlot(0, 2, wav.file, peaks.file, kRate);
    ASSERT_GE(slot, 0);
    for (int block = 0; block < 2; ++block)
        rig.render();
    ASSERT_TRUE(recorder.stopSlot(slot).ok);
    expectTakeHoldsChannels(wav.file, {0, 1}, 2 * kBlock);
}

TEST(InputTakeRecorderTest, AChannelTheDeviceDoesNotDeliverRecordsSilence) {
    HostedRig rig(2);
    ScopedTakeFile wav("agentsynth_input_missing.wav");
    ScopedTakeFile peaks("agentsynth_input_missing.agpk");
    auto& recorder = rig.engine.getInputRecorder();
    const int slot = recorder.startSlot(3, 1, wav.file, peaks.file, kRate);
    ASSERT_GE(slot, 0);
    rig.render();
    rig.render();
    const auto result = recorder.stopSlot(slot);
    ASSERT_TRUE(result.ok);
    const auto contents = readTakeWav(wav.file);
    ASSERT_EQ(contents.lengthInSamples, 2 * kBlock) << "silence still keeps the take in step with the transport";
    EXPECT_EQ(contents.audio.getMagnitude(0, 0, 2 * kBlock), 0.0f);
    EXPECT_FALSE(rig.engine.isInputChannelActive(3));
    EXPECT_TRUE(rig.engine.isInputChannelActive(1));
}

TEST(InputTakeRecorderTest, StripSlotRecordsExactlyWhatItsBufferHeldAndABlockWithNoWriteIsSilence) {
    synth::InputTakeRecorder recorder;
    ScopedTakeFile wav("agentsynth_strip_slot.wav");
    ScopedTakeFile peaks("agentsynth_strip_slot.agpk");
    const int slot = recorder.startStripSlot(wav.file, peaks.file, kRate);
    ASSERT_GE(slot, 0);
    auto* buffer = recorder.getStripSlotBuffer(slot);
    ASSERT_NE(buffer, nullptr);
    ASSERT_GE(buffer->getNumChannels(), 2);
    ASSERT_GE(buffer->getNumSamples(), kBlock);

    // Block 0: the strip wrote. Block 1: it did not run. Block 2: it wrote again.
    for (int i = 0; i < kBlock; ++i) {
        buffer->setSample(0, i, inputSample(0, i));
        buffer->setSample(1, i, inputSample(1, i));
    }
    recorder.pushStripSlots(kBlock, 1000);
    EXPECT_EQ(buffer->getMagnitude(0, kBlock), 0.0f) << "the pushed frames are cleared for the next block";
    recorder.pushStripSlots(kBlock, 1000 + kBlock);
    for (int i = 0; i < kBlock; ++i) {
        buffer->setSample(0, i, inputSample(2, i));
        buffer->setSample(1, i, inputSample(3, i));
    }
    recorder.pushStripSlots(kBlock, 1000 + 2 * kBlock);
    const auto result = recorder.stopSlot(slot);

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.lengthSamples, 3 * kBlock);
    ASSERT_TRUE(result.captureStartValid);
    EXPECT_EQ(result.captureStartTimelineSample, 1000);
    const auto contents = readTakeWav(wav.file);
    ASSERT_EQ(contents.numChannels, 2);
    ASSERT_EQ(contents.lengthInSamples, 3 * kBlock);
    for (int i = 0; i < kBlock; ++i) {
        ASSERT_EQ(contents.audio.getSample(0, i), inputSample(0, i));
        ASSERT_EQ(contents.audio.getSample(1, i), inputSample(1, i));
        ASSERT_EQ(contents.audio.getSample(0, kBlock + i), 0.0f);
        ASSERT_EQ(contents.audio.getSample(1, kBlock + i), 0.0f);
        ASSERT_EQ(contents.audio.getSample(0, 2 * kBlock + i), inputSample(2, i));
        ASSERT_EQ(contents.audio.getSample(1, 2 * kBlock + i), inputSample(3, i));
    }
}

TEST(InputTakeRecorderTest, StripSlotsAndDeviceSlotsRecordSideBySideEachFromItsOwnSource) {
    synth::InputTakeRecorder recorder;
    ScopedTakeFile stripWav("agentsynth_strip_side.wav");
    ScopedTakeFile stripPeaks("agentsynth_strip_side.agpk");
    ScopedTakeFile deviceWav("agentsynth_device_side.wav");
    ScopedTakeFile devicePeaks("agentsynth_device_side.agpk");
    const int strip = recorder.startStripSlot(stripWav.file, stripPeaks.file, kRate);
    const int device = recorder.startSlot(0, 1, deviceWav.file, devicePeaks.file, kRate);
    ASSERT_GE(strip, 0);
    ASSERT_GE(device, 0);
    ASSERT_NE(strip, device);

    std::vector<float> input(kBlock);
    for (int i = 0; i < kBlock; ++i)
        input[(std::size_t)i] = inputSample(5, i);
    const float* channels[1] = {input.data()};
    auto* buffer = recorder.getStripSlotBuffer(strip);
    for (int i = 0; i < kBlock; ++i) {
        buffer->setSample(0, i, 0.25f);
        buffer->setSample(1, i, -0.25f);
    }
    recorder.push(channels, 1, kBlock, 0, true);
    recorder.pushStripSlots(kBlock, 0);
    ASSERT_TRUE(recorder.stopSlot(strip).ok);
    ASSERT_TRUE(recorder.stopSlot(device).ok);

    const auto stripTake = readTakeWav(stripWav.file);
    ASSERT_EQ(stripTake.lengthInSamples, kBlock);
    EXPECT_EQ(stripTake.audio.getSample(0, 10), 0.25f);
    EXPECT_EQ(stripTake.audio.getSample(1, 10), -0.25f);
    const auto deviceTake = readTakeWav(deviceWav.file);
    ASSERT_EQ(deviceTake.numChannels, 1);
    ASSERT_EQ(deviceTake.lengthInSamples, kBlock) << "a device push is not fed twice by the strip push";
    for (int i = 0; i < kBlock; ++i)
        ASSERT_EQ(deviceTake.audio.getSample(0, i), inputSample(5, i));
}

TEST(InputTakeRecorderTest, SlotsRefuseBadChannelsAndStoppingAnythingIsSafe) {
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    auto& recorder = engine.getInputRecorder();
    ScopedTakeFile wav("agentsynth_input_refuse.wav");
    ScopedTakeFile peaks("agentsynth_input_refuse.agpk");
    EXPECT_EQ(recorder.startSlot(-1, 1, wav.file, peaks.file, kRate), -1);
    EXPECT_EQ(recorder.startSlot(7, 2, wav.file, peaks.file, kRate), -1);
    EXPECT_EQ(recorder.startSlot(0, 3, wav.file, peaks.file, kRate), -1);
    EXPECT_FALSE(recorder.stopSlot(-1).ok);
    EXPECT_FALSE(recorder.stopSlot(synth::InputTakeRecorder::kMaxSlots).ok);
    EXPECT_FALSE(recorder.stopSlot(0).ok) << "a slot that is not capturing";
    std::vector<std::pair<float, float>> peaksOut{{1.0f, 1.0f}};
    recorder.copyLivePeaks(-1, peaksOut);
    EXPECT_TRUE(peaksOut.empty());
}

TEST(InputTakeRecorderTest, ARateChangeHaltsInputTakesAndFlagsTheCommit) {
    HostedRig rig(2);
    ScopedTakeFile wav("agentsynth_input_rate.wav");
    ScopedTakeFile peaks("agentsynth_input_rate.agpk");
    auto& recorder = rig.engine.getInputRecorder();
    const int slot = recorder.startSlot(0, 1, wav.file, peaks.file, kRate);
    ASSERT_GE(slot, 0);
    rig.render();
    EXPECT_FALSE(rig.engine.consumeFormatChangedDuringCapture());

    rig.engine.prepareForHost(44100.0, kBlock, 2, 2);
    EXPECT_TRUE(rig.engine.consumeFormatChangedDuringCapture()) << "the poll must commit the input take";
    rig.render();
    const auto result = recorder.stopSlot(slot);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.lengthSamples, kBlock) << "not one block of the new rate goes into the old-rate file";
    EXPECT_DOUBLE_EQ(readTakeWav(wav.file).sampleRate, kRate);
}

TEST(InputTakeRecorderTest, InputPeaksAreReadAndResetPerChannel) {
    HostedRig rig(3);
    rig.render();
    EXPECT_FLOAT_EQ(rig.engine.readInputPeak(2), inputSample(2, 255));
    EXPECT_EQ(rig.engine.readInputPeak(2), 0.0f) << "a read resets the peak";
    EXPECT_FLOAT_EQ(rig.engine.readInputPeak(0), inputSample(0, 255));
    EXPECT_EQ(rig.engine.readInputPeak(5), 0.0f) << "an input the host does not have";
}

TEST(InputTakeRecorderTest, ShutdownFinalisesARollingInputTake) {
    ScopedTakeFile wav("agentsynth_input_shutdown.wav");
    ScopedTakeFile peaks("agentsynth_input_shutdown.agpk");
    {
        HostedRig rig(2);
        ASSERT_GE(rig.engine.getInputRecorder().startSlot(0, 1, wav.file, peaks.file, kRate), 0);
        rig.render();
        rig.engine.shutdown();
        EXPECT_FALSE(rig.engine.getInputRecorder().isAnyCapturing());
    }
    EXPECT_EQ(readTakeWav(wav.file).lengthInSamples, kBlock);
    EXPECT_TRUE(peaks.file.existsAsFile());
}

namespace {

// A device with only inputs 3 and 4 turned on: JUCE hands the callback those two, packed as channels 0 and 1.
class SparseInputDevice : public synth::test::FakeAudioIODevice {
public:
    SparseInputDevice()
        : FakeAudioIODevice(2, 2) {}
    juce::BigInteger getActiveInputChannels() const override {
        juce::BigInteger mask;
        mask.setBit(2);
        mask.setBit(3);
        return mask;
    }
};

} // namespace

TEST(InputTakeRecorderTest, ATakeNamesDeviceInputsNotPackedCallbackChannels) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    SparseInputDevice device;
    engine.audioDeviceAboutToStart(&device);
    EXPECT_FALSE(engine.isInputChannelActive(0));
    EXPECT_TRUE(engine.isInputChannelActive(2));
    EXPECT_TRUE(engine.isInputChannelActive(3));

    ScopedTakeFile wav("agentsynth_input_sparse.wav");
    ScopedTakeFile peaks("agentsynth_input_sparse.agpk");
    const int slot = engine.getInputRecorder().startSlot(3, 1, wav.file, peaks.file, kRate);
    ASSERT_GE(slot, 0);

    std::vector<float> in3((std::size_t)kBlock, 0.25f), in4((std::size_t)kBlock, 0.5f);
    std::vector<float> outL((std::size_t)kBlock), outR((std::size_t)kBlock);
    const float* inputs[] = {in3.data(), in4.data()};
    float* outputs[] = {outL.data(), outR.data()};
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, kBlock, {});
    ASSERT_TRUE(engine.getInputRecorder().stopSlot(slot).ok);
    engine.audioDeviceStopped();

    const auto contents = readTakeWav(wav.file);
    ASSERT_EQ(contents.lengthInSamples, kBlock);
    EXPECT_EQ(contents.audio.getSample(0, 10), 0.5f) << "device input 4 is the second packed channel";
}

TEST(InputTakeRecorderTest, ManySlotsDrainOnOneThreadWhileAnotherThreadPushes) {
    synth::InputTakeRecorder recorder;
    std::vector<std::unique_ptr<ScopedTakeFile>> files;
    std::atomic<bool> running{true};
    std::thread audio([&] {
        std::vector<float> a((std::size_t)kBlock, 0.125f), b((std::size_t)kBlock, -0.125f);
        const float* inputs[] = {a.data(), b.data()};
        juce::int64 position = 0;
        while (running.load()) {
            recorder.push(inputs, 2, kBlock, position, true);
            position += kBlock;
            std::this_thread::yield();
        }
    });
    for (int round = 0; round < 3; ++round) {
        std::vector<int> slots;
        for (int i = 0; i < 4; ++i) {
            files.push_back(std::make_unique<ScopedTakeFile>("agentsynth_input_race_" + juce::String(round) + "_" +
                                                             juce::String(i) + ".wav"));
            const auto peaks = files.back()->file.withFileExtension("agpk");
            slots.push_back(recorder.startSlot(i % 2, 1, files.back()->file, peaks, kRate));
            ASSERT_GE(slots.back(), 0);
        }
        std::vector<std::pair<float, float>> live;
        for (int i = 0; i < 10; ++i)
            for (const int slot : slots)
                recorder.copyLivePeaks(slot, live);
        for (const int slot : slots) {
            const auto result = recorder.stopSlot(slot);
            EXPECT_TRUE(result.ok);
        }
        for (int i = 0; i < 4; ++i)
            files[files.size() - 4 + (std::size_t)i]->file.withFileExtension("agpk").deleteFile();
    }
    running.store(false);
    audio.join();
}

TEST(DeviceInputEnableTest, PicksTheChosenThenTheOutputDeviceThenTheDefault) {
    const juce::StringArray inputs{"Built-in Microphone", "Scarlett 4i4 USB"};
    EXPECT_EQ(synth::chooseInputDeviceToEnable("Scarlett 4i4 USB", "Speakers", inputs, "Built-in Microphone"),
              "Scarlett 4i4 USB");
    EXPECT_EQ(synth::chooseInputDeviceToEnable({}, "Scarlett 4i4 USB", inputs, "Built-in Microphone"),
              "Scarlett 4i4 USB")
        << "an interface that plays and records keeps one device";
    EXPECT_EQ(synth::chooseInputDeviceToEnable({}, "Speakers", inputs, "Built-in Microphone"), "Built-in Microphone");
}

TEST(DeviceInputEnableTest, TurnsOnEveryInputUpToEightExplicitly) {
    juce::AudioDeviceManager::AudioDeviceSetup current;
    current.outputDeviceName = "Scarlett 4i4 USB";
    current.useDefaultInputChannels = true;
    current.sampleRate = 48000.0;

    const auto four = synth::makeInputEnabledSetup(current, "Scarlett 4i4 USB", 4);
    EXPECT_EQ(four.inputDeviceName, "Scarlett 4i4 USB");
    EXPECT_FALSE(four.useDefaultInputChannels) << "explicit channels are what persists across a relaunch";
    EXPECT_EQ(four.inputChannels.countNumberOfSetBits(), 4);
    EXPECT_TRUE(four.inputChannels[0] && four.inputChannels[3]);
    EXPECT_EQ(four.outputDeviceName, "Scarlett 4i4 USB") << "the output side is left alone";
    EXPECT_DOUBLE_EQ(four.sampleRate, 48000.0);

    EXPECT_EQ(synth::makeInputEnabledSetup(current, "Big Rack", 18).inputChannels.countNumberOfSetBits(), 8);
}

TEST(DeviceInputEnableTest, HostedEngineReportsTheHostInputsAndIgnoresEnable) {
    HostedRig rig(4);
    const auto info = rig.engine.getAudioInputInfo();
    EXPECT_TRUE(info.hosted);
    EXPECT_EQ(info.activeInputs, 4);
    EXPECT_EQ(info.deviceInputs, 4);
    EXPECT_TRUE(info.deviceName.isEmpty());
    rig.engine.enableDeviceInputs(); // a no-op: the host owns its inputs
    EXPECT_EQ(rig.engine.getAudioInputInfo().activeInputs, 4);
}
