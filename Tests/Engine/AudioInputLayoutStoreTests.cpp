// AudioInputLayoutStoreTests.cpp: the per-interface input layout (pairs and names), its JSON form, and the pure
// device setup the Settings Inputs table's Use tick builds.
#include "AudioEngine/AudioInputLayoutStore.h"
#include "AudioEngine/DeviceInputEnable.h"
#include <gtest/gtest.h>

namespace {
using synth::AudioInputLayoutStore;
using synth::InputSlot;

const juce::String kType = "CoreAudio";
const juce::String kVolt = "Volt 2 (studio)";
const juce::String kScarlett = "Scarlett 4i4";
} // namespace

TEST(AudioInputLayoutStoreTest, AFirstSightingGetsAnEntryWithNoPairsAndNoNames) {
    AudioInputLayoutStore store;
    EXPECT_FALSE(store.has(kType, kVolt));
    store.touch(kType, kVolt, 100);
    ASSERT_TRUE(store.has(kType, kVolt));
    const auto entries = store.entries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_TRUE(entries[0].pairedPairs.empty());
    EXPECT_TRUE(entries[0].names.empty());
    EXPECT_EQ(entries[0].lastUsedMs, 100);
}

TEST(AudioInputLayoutStoreTest, PairingJoinsNeighbouringJacksIntoOneStereoSlot) {
    AudioInputLayoutStore store;
    EXPECT_EQ(store.slotsFor(kType, kVolt, 4).size(), 4u) << "four mono jacks to begin with";

    store.setPaired(kType, kVolt, 1, true);
    const auto slots = store.slotsFor(kType, kVolt, 4);
    ASSERT_EQ(slots.size(), 3u);
    EXPECT_EQ(slots[2].firstJack, 2);
    EXPECT_EQ(slots[2].numJacks, 2);

    store.setPaired(kType, kVolt, 1, false);
    EXPECT_EQ(store.slotsFor(kType, kVolt, 4).size(), 4u) << "unpairing hands the two mono jacks back";
}

TEST(AudioInputLayoutStoreTest, AnOddLastJackCannotBePaired) {
    AudioInputLayoutStore store;
    store.setPaired(kType, kVolt, 1, true);
    const auto slots = store.slotsFor(kType, kVolt, 3);
    ASSERT_EQ(slots.size(), 3u);
    EXPECT_EQ(slots[2].firstJack, 2);
    EXPECT_EQ(slots[2].numJacks, 1);
}

TEST(AudioInputLayoutStoreTest, NamesAreKeptThroughPairingAndUnpairing) {
    AudioInputLayoutStore store;
    store.setName(kType, kVolt, 0, "Vocal mic");
    store.setName(kType, kVolt, 1, "DI");
    store.setPaired(kType, kVolt, 0, true);
    auto slots = store.slotsFor(kType, kVolt, 2);
    ASSERT_EQ(slots.size(), 1u);
    EXPECT_EQ(slots[0].name, "Vocal mic") << "a stereo input is named by its first jack";
    store.setPaired(kType, kVolt, 0, false);
    slots = store.slotsFor(kType, kVolt, 2);
    ASSERT_EQ(slots.size(), 2u);
    EXPECT_EQ(slots[0].name, "Vocal mic");
    EXPECT_EQ(slots[1].name, "DI");
    EXPECT_EQ(store.countNamedInputs(kType, kVolt, 2), 2);
}

TEST(AudioInputLayoutStoreTest, AnEmptyNameClearsAndNamesAreTrimmed) {
    AudioInputLayoutStore store;
    store.setName(kType, kVolt, 0, "  Vocal mic ");
    EXPECT_EQ(store.getName(kType, kVolt, 0), "Vocal mic");
    store.setName(kType, kVolt, 0, "   ");
    EXPECT_EQ(store.getName(kType, kVolt, 0), juce::String());
}

TEST(AudioInputLayoutStoreTest, JsonRoundTripKeepsEveryInterface) {
    AudioInputLayoutStore store;
    store.touch(kType, kVolt, 500);
    store.setPaired(kType, kVolt, 1, true);
    store.setName(kType, kVolt, 0, "Vocal mic");
    store.setName(kType, kVolt, 2, "Synth \"left\" + right");
    store.touch(kType, kScarlett, 900);

    AudioInputLayoutStore loaded;
    loaded.loadJson(store.toJson());
    ASSERT_EQ(loaded.entries().size(), 2u);
    EXPECT_EQ(loaded.entries()[0].deviceName, kScarlett) << "most recently used first";
    EXPECT_TRUE(loaded.isPaired(kType, kVolt, 1));
    EXPECT_FALSE(loaded.isPaired(kType, kVolt, 0));
    EXPECT_EQ(loaded.getName(kType, kVolt, 0), "Vocal mic");
    EXPECT_EQ(loaded.getName(kType, kVolt, 2), "Synth \"left\" + right");
    EXPECT_EQ(loaded.entries()[1].lastUsedMs, 500);
}

TEST(AudioInputLayoutStoreTest, MalformedJsonLoadsAsEmptyWithoutCrashing) {
    AudioInputLayoutStore store;
    store.touch(kType, kVolt);
    for (const char* bad : {"", "not json", "{", "[]", "{\"interfaces\": 5}", "{\"interfaces\": [1, \"x\", null]}",
                            "{\"interfaces\": [{\"name\": 3, \"type\": []}]}",
                            "{\"interfaces\": [{\"name\": \"A\", \"type\": \"B\", \"paired\": [99, -1, \"x\"], "
                            "\"names\": {\"77\": \"far\", \"x\": 3}}]}"}) {
        EXPECT_NO_THROW(store.loadJson(bad)) << bad;
        for (const auto& entry : store.entries()) {
            EXPECT_TRUE(entry.pairedPairs.empty()) << bad;
            EXPECT_TRUE(entry.names.empty()) << bad;
        }
    }
    store.loadJson("garbage");
    EXPECT_TRUE(store.entries().empty());
}

TEST(AudioInputLayoutStoreTest, SwitchingDeviceAndBackKeepsTheLayoutAndAMissingDeviceKeepsItsEntry) {
    AudioInputLayoutStore store;
    store.setPaired(kType, kVolt, 0, true);
    store.setName(kType, kVolt, 0, "Vocal mic");
    // The user plugs in another interface, uses it, and the first one goes away.
    store.touch(kType, kScarlett);
    store.setName(kType, kScarlett, 0, "Guitar");
    store.touch(kType, kScarlett);
    EXPECT_TRUE(store.has(kType, kVolt)) << "an interface that is not connected keeps its entry";
    EXPECT_EQ(store.getName(kType, kScarlett, 0), "Guitar");

    // ... and back: the pair and the name are as they were left.
    store.touch(kType, kVolt);
    EXPECT_TRUE(store.isPaired(kType, kVolt, 0));
    EXPECT_EQ(store.getName(kType, kVolt, 0), "Vocal mic");
    EXPECT_FALSE(store.isPaired(kType, kScarlett, 0)) << "layouts are per interface";
}

TEST(AudioInputLayoutStoreTest, TheSameNameOnAnotherDeviceTypeIsAnotherInterface) {
    AudioInputLayoutStore store;
    store.setName("CoreAudio", kVolt, 0, "Vocal mic");
    EXPECT_EQ(store.getName("ASIO", kVolt, 0), juce::String());
}

TEST(AudioInputLayoutStoreTest, ForgetRemovesOnlyThatInterface) {
    AudioInputLayoutStore store;
    store.setName(kType, kVolt, 0, "Vocal mic");
    store.setName(kType, kScarlett, 0, "Guitar");
    EXPECT_FALSE(store.forget(kType, "Unknown"));
    EXPECT_TRUE(store.forget(kType, kVolt));
    EXPECT_FALSE(store.has(kType, kVolt));
    EXPECT_EQ(store.getName(kType, kScarlett, 0), "Guitar");
}

TEST(AudioInputLayoutStoreTest, EveryChangeCallsOnChangeAndTellsListeners) {
    struct Counter : AudioInputLayoutStore::Listener {
        int calls = 0;
        void inputLayoutChanged() override { ++calls; }
    } counter;
    AudioInputLayoutStore store;
    store.addListener(&counter);
    int saved = 0;
    store.onChange = [&saved] { ++saved; };
    store.setPaired(kType, kVolt, 0, true);
    store.setPaired(kType, kVolt, 0, true); // no change
    store.setName(kType, kVolt, 0, "Vocal mic");
    store.forget(kType, kVolt);
    EXPECT_EQ(saved, 3);
    EXPECT_EQ(counter.calls, 3);
    store.removeListener(&counter);
}

TEST(DeviceInputEnableTest, ToggleOneJackKeepsTheOthersAsTheyWere) {
    juce::AudioDeviceManager::AudioDeviceSetup current;
    const auto on = synth::makeInputJacksToggledSetup(current, "Volt 2", 0b0101, 1, 1, true);
    EXPECT_EQ(on.inputDeviceName, "Volt 2");
    EXPECT_FALSE(on.useDefaultInputChannels);
    EXPECT_TRUE(on.inputChannels[0]);
    EXPECT_TRUE(on.inputChannels[1]);
    EXPECT_TRUE(on.inputChannels[2]);
    EXPECT_FALSE(on.inputChannels[3]);

    const auto off = synth::makeInputJacksToggledSetup(current, "Volt 2", 0b0111, 0, 2, false);
    EXPECT_FALSE(off.inputChannels[0]);
    EXPECT_FALSE(off.inputChannels[1]);
    EXPECT_TRUE(off.inputChannels[2]) << "a stereo input switches both jacks together";
}
