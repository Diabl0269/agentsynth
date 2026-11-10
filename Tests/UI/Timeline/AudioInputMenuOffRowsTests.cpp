// AudioInputMenuOffRowsTests.cpp: a greyed input row says it is off, the way to turn the inputs on sits right under the
// header where the grey rows are, and every disabled row of the menu states why (the silent-disabled guard).
#include "UI/Timeline/TimelineRoutingPane/AudioInputMenu.h"
#include <gtest/gtest.h>

namespace {
using synth::ui::AudioInputMenuIds;

struct Row {
    int id = 0;
    juce::String text;
    bool enabled = true;
    bool header = false;
};

std::vector<Row> rowsOf(const juce::PopupMenu& menu) {
    std::vector<Row> rows;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next()) {
        const auto& item = it.getItem();
        if (!item.isSeparator)
            rows.push_back({item.itemID, item.text, item.isEnabled, item.isSectionHeader});
    }
    return rows;
}

const Row* findRow(const std::vector<Row>& rows, int id) {
    for (const auto& row : rows)
        if (row.id == id)
            return &row;
    return nullptr;
}

synth::AudioInputStatus scarlett(int active, std::uint32_t mask) {
    synth::AudioInputStatus status;
    status.deviceName = "Scarlett 4i4";
    status.deviceInputs = 4;
    status.activeInputs = active;
    status.activeMask = mask;
    return status;
}

// A disabled row of this menu must carry its reason in its own words.
bool statesWhy(const juce::String& text) {
    for (const char* why :
         {"(off)", "turned off", "no mixer channel", "No audio input", "passes no inputs", "no other tracks"})
        if (text.contains(why))
            return true;
    return false;
}
} // namespace

TEST(AudioInputMenuOffRows, OffMonoAndStereoRowsSayOffAndTheTurnOnItemSitsUnderTheMonoHeader) {
    const auto rows = rowsOf(synth::ui::buildAudioInputMenu({}, scarlett(2, 0b0011)));
    EXPECT_EQ(findRow(rows, AudioInputMenuIds::kFirstMono + 0)->text, "In 1");
    EXPECT_EQ(findRow(rows, AudioInputMenuIds::kFirstMono + 2)->text, "In 3 (off)");
    EXPECT_EQ(findRow(rows, AudioInputMenuIds::kFirstStereo + 0)->text, "In 1 + 2");
    EXPECT_EQ(findRow(rows, AudioInputMenuIds::kFirstStereo + 2)->text, "In 3 + 4 (off)");

    int header = -1, turnOn = -1, firstMono = -1;
    for (int i = 0; i < (int)rows.size(); ++i) {
        if (rows[(size_t)i].header && header < 0)
            header = i;
        if (rows[(size_t)i].id == AudioInputMenuIds::kTurnOnInputs)
            turnOn = i;
        if (rows[(size_t)i].id == AudioInputMenuIds::kFirstMono && firstMono < 0)
            firstMono = i;
    }
    ASSERT_GE(turnOn, 0);
    EXPECT_EQ(turnOn, header + 1) << "right under the Mono header";
    EXPECT_LT(turnOn, firstMono);
}

TEST(AudioInputMenuOffRows, AStereoPairWithOneLegOffIsOff) {
    const auto rows = rowsOf(synth::ui::buildAudioInputMenu({}, scarlett(3, 0b0111)));
    EXPECT_EQ(findRow(rows, AudioInputMenuIds::kFirstStereo + 2)->text, "In 3 + 4 (off)");
    EXPECT_FALSE(findRow(rows, AudioInputMenuIds::kFirstStereo + 2)->enabled);
}

TEST(AudioInputMenuOffRows, WithAllInputsOnNothingSaysOffAndThereIsNoTurnOnItem) {
    const auto rows = rowsOf(synth::ui::buildAudioInputMenu({}, scarlett(4, 0b1111)));
    EXPECT_EQ(findRow(rows, AudioInputMenuIds::kTurnOnInputs), nullptr);
    for (const auto& row : rows)
        EXPECT_FALSE(row.text.contains("(off)")) << row.text;
}

// Guard for the silent-disabled kind: in every state the menu can be in, each greyed row says why.
TEST(AudioInputMenuOffRows, EveryDisabledRowInEveryMenuStateStatesWhy) {
    synth::AudioInputStatus hosted;
    hosted.hosted = true;
    synth::AudioInputStatus hostedNone = hosted;
    synth::AudioInputStatus noDevice;
    std::vector<synth::AudioInputStatus> statuses{
        scarlett(2, 0b0011), scarlett(3, 0b0111), scarlett(0, 0), scarlett(4, 0b1111), noDevice, hostedNone};
    hosted.activeInputs = 2;
    hosted.activeMask = 0b11;
    statuses.push_back(hosted);
    const std::vector<std::vector<synth::ui::AudioInputTrackSource>> sourceSets{
        {}, {{synth::TrackId{4}, "Lead", true}, {synth::TrackId{9}, "Bass", false}}};
    for (const auto& status : statuses)
        for (const auto& sources : sourceSets)
            for (const auto& row : rowsOf(synth::ui::buildAudioInputMenu({}, status, sources)))
                if (!row.header && !row.enabled)
                    EXPECT_TRUE(statesWhy(row.text)) << "a greyed row says nothing: \"" << row.text << "\"";
}
