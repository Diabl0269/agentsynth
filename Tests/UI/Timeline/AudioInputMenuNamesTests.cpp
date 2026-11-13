// AudioInputMenuNamesTests.cpp: the audio Input menu with named and paired inputs -- the name goes on the mono entry
// of an unpaired jack and on the stereo entry of a paired pair, and the Mono and Stereo sections both stay.
#include "UI/Timeline/TimelineRoutingPane/AudioInputMenu.h"
#include <gtest/gtest.h>

namespace {
using synth::ui::AudioInputMenuIds;

synth::AudioInputStatus volt() {
    synth::AudioInputStatus status;
    status.deviceName = "Volt 2";
    status.deviceInputs = 4;
    status.activeInputs = 4;
    status.activeMask = 0b1111;
    // In 1 is a named mono jack, In 2 a plain one, In 3 + 4 a named stereo pair.
    status.layout = {{0, 1, "Vocal mic"}, {1, 1, ""}, {2, 2, "Synth"}};
    return status;
}

juce::String textOf(const juce::PopupMenu& menu, int id) {
    juce::PopupMenu::MenuItemIterator iterator(menu);
    while (iterator.next())
        if (iterator.getItem().itemID == id)
            return iterator.getItem().text;
    return "<missing>";
}

std::vector<juce::String> sectionHeaders(const juce::PopupMenu& menu) {
    std::vector<juce::String> headers;
    juce::PopupMenu::MenuItemIterator iterator(menu);
    while (iterator.next())
        if (iterator.getItem().isSectionHeader)
            headers.push_back(iterator.getItem().text);
    return headers;
}
} // namespace

TEST(AudioInputMenuNamesTest, ANamedMonoJackAndAPairedStereoInputShowTheirNames) {
    const auto menu = synth::ui::buildAudioInputMenu({}, volt());
    EXPECT_EQ(textOf(menu, AudioInputMenuIds::kFirstMono + 0), "Vocal mic (In 1)");
    EXPECT_EQ(textOf(menu, AudioInputMenuIds::kFirstMono + 1), "In 2");
    EXPECT_EQ(textOf(menu, AudioInputMenuIds::kFirstStereo + 2), "Synth (In 3 + 4)");
}

TEST(AudioInputMenuNamesTest, AnUnpairedPairsJacksAreNotNamedOnTheStereoEntryAndPairedOnesNotOnTheMonoEntries) {
    const auto menu = synth::ui::buildAudioInputMenu({}, volt());
    EXPECT_EQ(textOf(menu, AudioInputMenuIds::kFirstStereo + 0), "In 1 + 2") << "the pair 1-2 is not joined";
    EXPECT_EQ(textOf(menu, AudioInputMenuIds::kFirstMono + 2), "In 3")
        << "the pair 3-4 is joined: its name is not on the jacks";
    EXPECT_EQ(textOf(menu, AudioInputMenuIds::kFirstMono + 3), "In 4");
}

TEST(AudioInputMenuNamesTest, BothTheMonoAndTheStereoSectionAreStillListed) {
    const auto menu = synth::ui::buildAudioInputMenu({}, volt());
    const auto headers = sectionHeaders(menu);
    ASSERT_EQ(headers.size(), 2u);
    EXPECT_TRUE(headers[0].startsWith("Mono"));
    EXPECT_TRUE(headers[1].startsWith("Stereo"));
    for (int channel = 0; channel < 4; ++channel)
        EXPECT_NE(textOf(menu, AudioInputMenuIds::kFirstMono + channel), "<missing>");
    EXPECT_NE(textOf(menu, AudioInputMenuIds::kFirstStereo), "<missing>");
    EXPECT_NE(textOf(menu, AudioInputMenuIds::kFirstStereo + 2), "<missing>");
}

TEST(AudioInputMenuNamesTest, APickedNamedInputIsTickedWhereverItNowLives) {
    synth::AudioInputChoice choice;
    choice.kind = synth::AudioInputChoice::Kind::Device;
    choice.firstChannel = 0;
    choice.numChannels = 1;
    choice.deviceName = "Scarlett";
    choice.inputName = "Synth";
    const auto menu = synth::ui::buildAudioInputMenu(choice, volt());
    bool stereoTicked = false, monoTicked = false;
    juce::PopupMenu::MenuItemIterator iterator(menu);
    while (iterator.next()) {
        stereoTicked = stereoTicked || (iterator.getItem().itemID == AudioInputMenuIds::kFirstStereo + 2 &&
                                        iterator.getItem().isTicked);
        monoTicked =
            monoTicked || (iterator.getItem().itemID == AudioInputMenuIds::kFirstMono && iterator.getItem().isTicked);
    }
    EXPECT_TRUE(stereoTicked);
    EXPECT_FALSE(monoTicked) << "the jacks it was picked on are not where it is now";
}
