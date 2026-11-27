// AudioDeviceSectionTests.cpp: the Audio tab's Device section picking an output device, against a fake device type
// whose second device has a different default buffer size from the first, and its Render cores row.
#include "../../FakeAudioIODevice.h"
#include "../../TestWait.h"
#include "../../UniqueTempDir.h"
#include "../Accessibility/TabOrderHelpers.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/RenderCores.h"
#include "MainComponent/MainComponentAudioSettingsSource.h"
#include "UI/Settings/AudioSettingsTab/AudioDeviceSection.h"
#include <gtest/gtest.h>

namespace {
constexpr int kPodsDefaultBuffer = 256;
bool gPodsRejectLargeBuffer = false;

// Offers 256 and 512 samples but defaults to 256, as a Bluetooth output might.
class PodsDevice : public synth::test::FakeAudioIODevice {
public:
    PodsDevice()
        : FakeAudioIODevice(0, 2, 0, 0, synth::test::kFakeDeviceSampleRate, kPodsDefaultBuffer) {}

    juce::Array<int> getAvailableBufferSizes() override { return {kPodsDefaultBuffer, 512}; }

    // Reports the size it was opened with, as a real device does.
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int bufferSize) override {
        if (gPodsRejectLargeBuffer && bufferSize > kPodsDefaultBuffer)
            return "buffer too large";
        opened_ = bufferSize;
        return {};
    }
    int getCurrentBufferSizeSamples() override { return opened_; }

private:
    int opened_ = kPodsDefaultBuffer;
};

// A device that never opens.
class BrokenDevice : public synth::test::FakeAudioIODevice {
public:
    BrokenDevice()
        : FakeAudioIODevice(0, 2) {}
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return "boom"; }
};

class TwoDeviceType : public juce::AudioIODeviceType {
public:
    TwoDeviceType()
        : AudioIODeviceType("Fake") {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override { return {"Speakers", "Pods", "Broken"}; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice(const juce::String& outputName, const juce::String&) override {
        if (outputName == "Pods")
            return new PodsDevice();
        if (outputName == "Broken")
            return new BrokenDevice();
        if (outputName == "Speakers")
            return new synth::test::FakeAudioIODevice(0, 2);
        return nullptr;
    }
};
} // namespace

namespace {
struct Rig {
    Rig() {
        manager.addAudioDeviceType(std::make_unique<TwoDeviceType>());
        manager.setCurrentAudioDeviceType("Fake", true);
        auto setup = manager.getAudioDeviceSetup();
        setup.outputDeviceName = "Speakers";
        setup.sampleRate = synth::test::kFakeDeviceSampleRate;
        setup.bufferSize = synth::test::kFakeDeviceBlockSize;
        EXPECT_TRUE(manager.setAudioDeviceSetup(setup, true).isEmpty());
        gPodsRejectLargeBuffer = false;
        // Built last: constructing it earlier makes the manager create the machine's real device types first.
        section = std::make_unique<AudioDeviceSection>(manager, source);
        section->setSize(520, 300);
    }
    ~Rig() { gPodsRejectLargeBuffer = false; }
    void pick(int id) { section->getOutputBoxForTest().setSelectedId(id, juce::sendNotificationSync); }

    juce::AudioDeviceManager manager;
    DeviceManagerSettingsSource source{manager};
    std::unique_ptr<AudioDeviceSection> section;
};
} // namespace

// A device picked before comes back with what the user last had on it.
TEST(AudioDeviceSectionTest, APickedDeviceComesBackWithItsRememberedBufferAndChannels) {
    Rig rig;
    // The fake device names its own type, so the key is whatever the manager reports once a device is open.
    rig.source.layoutStore().setDeviceSettings(rig.manager.getCurrentAudioDeviceType(), "Pods",
                                               {synth::test::kFakeDeviceSampleRate, 512, 0b11});
    rig.pick(2);
    ASSERT_NE(rig.manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(rig.manager.getAudioDeviceSetup().outputDeviceName, "Pods");
    EXPECT_EQ(rig.manager.getAudioDeviceSetup().bufferSize, 512);
}

// Remembered values the device no longer accepts fall back to its own defaults instead of failing the pick.
TEST(AudioDeviceSectionTest, RememberedValuesThatNoLongerOpenFallBackToTheDeviceDefaults) {
    Rig rig;
    // The fake device names its own type, so the key is whatever the manager reports once a device is open.
    rig.source.layoutStore().setDeviceSettings(rig.manager.getCurrentAudioDeviceType(), "Pods",
                                               {synth::test::kFakeDeviceSampleRate, 512, 0b11});
    gPodsRejectLargeBuffer = true;
    rig.pick(2);
    ASSERT_NE(rig.manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(rig.manager.getAudioDeviceSetup().outputDeviceName, "Pods");
    EXPECT_EQ(rig.manager.getAudioDeviceSetup().bufferSize, kPodsDefaultBuffer);
}

// A device that will not open leaves the previous one playing, and the box still names it.
TEST(AudioDeviceSectionTest, ADeviceThatWillNotOpenLeavesThePreviousOneOpen) {
    Rig rig;
    juce::String failedDevice, failedError;
    rig.section->onOpenFailed = [&](const juce::String& name, const juce::String& error) {
        failedDevice = name;
        failedError = error;
    };
    rig.pick(3);
    EXPECT_EQ(failedDevice, "Broken");
    EXPECT_EQ(failedError, "boom");
    ASSERT_NE(rig.manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(rig.manager.getAudioDeviceSetup().outputDeviceName, "Speakers");
    EXPECT_EQ(rig.section->getOutputBoxForTest().getText(), "Speakers");
}

// Picking an output device opens it with its own default buffer size instead of carrying over the previous
// device's, and the box then shows the device that really opened.
TEST(AudioDeviceSectionTest, PickingAnOutputOpensItAtItsOwnDefaultBufferSize) {
    juce::AudioDeviceManager manager;
    manager.addAudioDeviceType(std::make_unique<TwoDeviceType>());
    manager.setCurrentAudioDeviceType("Fake", true);
    auto setup = manager.getAudioDeviceSetup();
    setup.outputDeviceName = "Speakers";
    setup.sampleRate = synth::test::kFakeDeviceSampleRate;
    setup.bufferSize = synth::test::kFakeDeviceBlockSize;
    ASSERT_TRUE(manager.setAudioDeviceSetup(setup, true).isEmpty());

    DeviceManagerSettingsSource source(manager);
    AudioDeviceSection section(manager, source);
    section.setSize(520, 300);

    auto& output = section.getOutputBoxForTest();
    output.setSelectedId(2, juce::sendNotificationSync);

    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(manager.getAudioDeviceSetup().outputDeviceName, "Pods");
    EXPECT_EQ(manager.getAudioDeviceSetup().bufferSize, kPodsDefaultBuffer);
    EXPECT_EQ(output.getText(), "Pods");
}

// The device type, sample rate and buffer size sit under a collapsed Advanced row; the output is always there.
TEST(AudioDeviceSectionTest, SampleRateAndBufferSitUnderACollapsedAdvancedRow) {
    Rig rig;
    auto& section = *rig.section;
    EXPECT_FALSE(section.isAdvancedOpen());
    EXPECT_TRUE(section.getOutputBoxForTest().isVisible());
    EXPECT_FALSE(section.getSampleRateBoxForTest().isShowing());
    const int collapsed = section.preferredHeight(520);

    section.setAdvancedOpen(true, false);
    EXPECT_TRUE(section.isAdvancedOpen());
    EXPECT_FLOAT_EQ(section.advancedProgressForTest(), 1.0f);
    EXPECT_TRUE(section.getSampleRateBoxForTest().isVisible());
    EXPECT_TRUE(section.getBufferBoxForTest().isVisible());
    EXPECT_EQ(section.preferredHeight(520), collapsed + 2 * AudioSettingsSection::kRowHeight);

    section.setAdvancedOpen(false, false);
    EXPECT_EQ(section.preferredHeight(520), collapsed);
}

// Return and Space on the focused Advanced row ask for the other state, the owner is told, and the name says which.
TEST(AudioDeviceSectionTest, TheAdvancedRowOpensWithReturnAndSpaceAndNamesItsState) {
    Rig rig;
    auto& toggle = rig.section->getAdvancedToggleForTest();
    std::vector<bool> told;
    rig.section->onAdvancedToggled = [&](bool open) { told.push_back(open); };
    EXPECT_TRUE(toggle.getTitle().contains("collapsed"));
    // A key press clicks on the next message.
    EXPECT_TRUE(toggle.pressKeyForTest(juce::KeyPress(juce::KeyPress::spaceKey)));
    synth::test::waitUntil([&] { return rig.section->isAdvancedOpen(); });
    EXPECT_TRUE(rig.section->isAdvancedOpen());
    EXPECT_TRUE(toggle.getTitle().contains("expanded"));
    EXPECT_TRUE(toggle.pressKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)));
    synth::test::waitUntil([&] { return !rig.section->isAdvancedOpen(); });
    EXPECT_FALSE(rig.section->isAdvancedOpen());
    EXPECT_EQ(told, (std::vector<bool>{true, false}));
}

// A Bluetooth output shows a one-line note and a wired one does not; the note adds height.
TEST(AudioDeviceSectionTest, ABluetoothOutputShowsANote) {
    Rig rig;
    rig.section->isBluetooth = [](const juce::String& name) { return name == "Pods"; };
    rig.section->refresh();
    EXPECT_FALSE(rig.section->getBluetoothNoteForTest().isVisible());
    const int wired = rig.section->preferredHeight(520);

    rig.pick(2);
    EXPECT_EQ(rig.manager.getAudioDeviceSetup().outputDeviceName, "Pods");
    EXPECT_TRUE(rig.section->getBluetoothNoteForTest().isVisible());
    EXPECT_GT(rig.section->preferredHeight(520), wired);

    rig.pick(1);
    EXPECT_FALSE(rig.section->getBluetoothNoteForTest().isVisible());
    EXPECT_EQ(rig.section->preferredHeight(520), wired);
}

// A Bluetooth microphone picked as the input shows the call-mode warning; "No device" and a wired input do not.
TEST(AudioDeviceSectionTest, ABluetoothMicrophoneAsInputShowsAWarning) {
    Rig rig;
    rig.section->isBluetooth = [](const juce::String& name) { return name == "Pods"; };
    rig.section->refresh();
    EXPECT_FALSE(rig.section->getMicWarningForTest().isVisible());

    rig.section->getInputBoxForTest().setSelectedId(2, juce::sendNotificationSync);
    ASSERT_EQ(rig.manager.getAudioDeviceSetup().inputDeviceName, "Pods");
    EXPECT_TRUE(rig.section->getMicWarningForTest().isVisible());

    rig.section->getInputBoxForTest().setSelectedId(1, juce::sendNotificationSync);
    EXPECT_FALSE(rig.section->getMicWarningForTest().isVisible());
}

// With no device open there is no rate or buffer to show, so the open Advanced row says what to do instead of
// opening onto nothing.
TEST(AudioDeviceSectionTest, AdvancedWithNoDeviceOpenShowsAHint) {
    Rig rig;
    auto& section = *rig.section;
    section.setAdvancedOpen(true, false);
    EXPECT_FALSE(section.getAdvancedHintForTest().isVisible());

    rig.manager.closeAudioDevice();
    section.refresh();
    EXPECT_TRUE(section.getAdvancedHintForTest().isVisible());
    EXPECT_FALSE(section.getSampleRateBoxForTest().isVisible());
    const int hinted = section.preferredHeight(520);

    rig.pick(2);
    EXPECT_FALSE(section.getAdvancedHintForTest().isVisible());
    EXPECT_EQ(section.preferredHeight(520), hinted + AudioSettingsSection::kRowHeight);
}

namespace {
// A source that renders on several cores, on an eight-core machine, and records what the row asks of it.
struct CoresSource : DeviceManagerSettingsSource {
    using DeviceManagerSettingsSource::DeviceManagerSettingsSource;
    int renderCores() override { return setting; }
    void setRenderCores(int cores) override {
        setting = cores;
        asked.push_back(cores);
    }
    int machineCores() override { return 8; }
    int setting = 1;
    std::vector<int> asked;
};
} // namespace

// Auto takes every core but one, at most four; a number is that many cores (the audio thread is one), never more
// than the machine has.
TEST(RenderCoresTest, TheSettingMapsToWorkerThreads) {
    EXPECT_EQ(synth::trackWorkersFor(0, 8), 3);
    EXPECT_EQ(synth::trackWorkersFor(0, 4), 2);
    EXPECT_EQ(synth::trackWorkersFor(0, 2), 0);
    EXPECT_EQ(synth::trackWorkersFor(0, 1), 0);
    EXPECT_EQ(synth::trackWorkersFor(1, 8), 0);
    EXPECT_EQ(synth::trackWorkersFor(3, 8), 2);
    EXPECT_EQ(synth::trackWorkersFor(8, 4), 3);
    EXPECT_EQ(synth::trackWorkersFor(20, 16), synth::kMaxRenderCores - 1);
}

// Render cores is one more Advanced row, a drop-down like the sample rate and buffer size above it, named for a
// screen reader, with a tooltip and a Tab stop; a pick reaches the source at once, with no device restart.
TEST(AudioDeviceSectionTest, RenderCoresSitsUnderAdvancedAndAppliesAtOnce) {
    Rig rig;
    CoresSource source{rig.manager};
    AudioDeviceSection section(rig.manager, source);
    section.setSize(520, 400);
    const int collapsed = section.preferredHeight(520);
    section.setAdvancedOpen(true, false);
    auto& cores = section.getRenderCoresBoxForTest();
    EXPECT_TRUE(cores.isVisible());
    EXPECT_EQ(section.preferredHeight(520), collapsed + 3 * AudioSettingsSection::kRowHeight);
    EXPECT_EQ(cores.getTitle(), "Render cores");
    EXPECT_TRUE(cores.getTooltip().contains("one core"));
    EXPECT_TRUE(cores.getWantsKeyboardFocus());
    EXPECT_GT(cores.getY(), section.getBufferBoxForTest().getY()) << "under Buffer size";
    ASSERT_EQ(cores.getNumItems(), 9);
    EXPECT_EQ(cores.getItemText(0), "Auto (4 cores)");
    EXPECT_EQ(cores.getItemText(1), "1 core (off)");
    EXPECT_EQ(cores.getItemText(8), "8 cores");
    EXPECT_EQ(cores.getText(), "1 core (off)");

    const auto before = rig.manager.getAudioDeviceSetup();
    cores.setSelectedId(4, juce::sendNotificationSync); // 3 cores
    EXPECT_EQ(source.asked, std::vector<int>{3});
    const auto after = rig.manager.getAudioDeviceSetup();
    EXPECT_EQ(after.outputDeviceName, before.outputDeviceName);
    EXPECT_EQ(after.bufferSize, before.bufferSize);

    section.refresh();
    EXPECT_EQ(cores.getText(), "3 cores") << "a refresh shows the saved setting, and does not ask again";
    EXPECT_EQ(source.asked.size(), 1u);

    const auto names = synth::test::walkTabOrder(section).names();
    EXPECT_TRUE(names.contains("Render cores")) << names.joinIntoString(", ");
}

// The plugin's source has no worker render (the host owns the threads), so the row is not there.
TEST(AudioDeviceSectionTest, RenderCoresIsHiddenForASourceWithoutWorkers) {
    Rig rig;
    rig.section->setAdvancedOpen(true, false);
    EXPECT_FALSE(rig.section->getRenderCoresBoxForTest().isVisible());
}

// The app's source saves the pick under "renderCores" and sets the engine's worker count from it at once, and a
// later launch applies what was saved.
TEST(AudioDeviceSectionTest, TheEngineSourceSavesRenderCoresAndAppliesItToTheEngine) {
    const auto folder = synth::test::uniqueTempPath("RenderCoresSettings", "");
    juce::PropertiesFile::Options options;
    options.applicationName = "RenderCoresTest";
    options.filenameSuffix = ".settings";
    options.folderName = folder.getFullPathName();
    juce::ApplicationProperties props;
    props.setStorageParameters(options);
    AudioEngine engine;
    synth::maincomponent::EngineSettingsSource source(engine, props);
    EXPECT_EQ(source.renderCores(), synth::kDefaultRenderCores);
    source.setRenderCores(3);
    EXPECT_EQ(props.getUserSettings()->getIntValue(synth::maincomponent::kRenderCoresKey), 3);
    EXPECT_EQ(engine.getTrackWorkerCount(), synth::trackWorkersFor(3, source.machineCores()));

    AudioEngine relaunched;
    synth::maincomponent::EngineSettingsSource again(relaunched, props);
    again.applySavedRenderCores();
    EXPECT_EQ(relaunched.getTrackWorkerCount(), synth::trackWorkersFor(3, again.machineCores()));
    props.closeFiles();
    folder.deleteRecursively();
}
