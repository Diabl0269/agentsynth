// Concern: KeyField, the closed box of a key choice: its text, and that its keys open the picker once and are consumed
// (so a focused field's Space never also toggles playback).
#include "Music/SongKey.h"
#include "UI/Layout/KeyField.h"
#include <gtest/gtest.h>

using synth::ui::KeyField;

TEST(KeyFieldTest, ShowsTheKeyOrTheNoKeyText) {
    KeyField field;
    EXPECT_EQ(field.getText(), "No key");
    field.setKey(synth::makeScale(9, synth::scalePresetIndex("Natural Minor")));
    EXPECT_EQ(field.getText(), "A Natural Minor");
    field.setKey(std::nullopt);
    field.setNoKeyText("None");
    EXPECT_EQ(field.getText(), "None");
}

TEST(KeyFieldTest, SpaceReturnAndDownOpenOnceAndAreConsumed) {
    KeyField field;
    int opened = 0;
    field.onOpen = [&] { ++opened; };
    for (const auto& key : {juce::KeyPress(juce::KeyPress::spaceKey), juce::KeyPress(juce::KeyPress::returnKey),
                            juce::KeyPress(juce::KeyPress::downKey)}) {
        const int before = opened;
        EXPECT_TRUE(field.keyPressed(key)) << key.getTextDescription();
        EXPECT_EQ(opened, before + 1);
    }
}

TEST(KeyFieldTest, AClickDoesNotTakeKeyboardFocusButTabReachesIt) {
    KeyField field;
    EXPECT_FALSE(field.getMouseClickGrabsKeyboardFocus());
    EXPECT_TRUE(field.getWantsKeyboardFocus());
}

TEST(KeyFieldTest, ADisabledFieldDoesNotOpen) {
    KeyField field;
    int opened = 0;
    field.onOpen = [&] { ++opened; };
    field.setEnabled(false);
    field.showPopup();
    EXPECT_EQ(opened, 0);
}

TEST(KeyFieldTest, FollowingReadsSongKeyBeforeTheKeyAndSaysSoToTheUser) {
    KeyField field;
    field.setKey(synth::makeScale(2, synth::scalePresetIndex("Dorian")));
    field.setFollowing(true);
    EXPECT_TRUE(field.isFollowing());
    EXPECT_EQ(field.getText(), juce::String::fromUTF8("Song key \xc2\xb7 D Dorian"));
    EXPECT_EQ(field.getTitle(), "Key, following song key");
    EXPECT_EQ(field.getTooltip(), "Following the song key. Click to give this its own key.");

    field.setFollowing(false);
    EXPECT_FALSE(field.isFollowing());
    EXPECT_EQ(field.getText(), juce::String::fromUTF8("D Dorian \xc2\xb7 own"));
    EXPECT_EQ(field.getTitle(), "Key, own key");
}
