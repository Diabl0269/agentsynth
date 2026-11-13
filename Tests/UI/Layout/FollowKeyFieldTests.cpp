// Concern: FollowKeyField, the Key field of something that follows the song key or has its own: what it reads and
// tells a screen reader, when it glows (only when the key it follows moves under it, never under reduced motion), and
// the picker it opens (Follow song key row on top, ticked while following).
#include "Music/SongKey.h"
#include "UI/Layout/FollowKeyField.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

using synth::makeScale;
using synth::scalePresetIndex;
using synth::ui::FollowKeyField;
using synth::ui::KeyPicker;

namespace {

const juce::String kDot = juce::String::fromUTF8(" \xc2\xb7 ");

struct MotionOn {
    MotionOn() { synth::ui::setReducedMotionForTest(false); }
    ~MotionOn() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

synth::MusicalScale dDorian() { return makeScale(2, scalePresetIndex("Dorian")); }
synth::MusicalScale eMinor() { return makeScale(4, scalePresetIndex("Natural Minor")); }
synth::MusicalScale aPhrygian() { return makeScale(9, scalePresetIndex("Phrygian")); }

} // namespace

TEST(FollowKeyFieldTest, AFollowerReadsSongKeyAndAnOwnKeyIsMarked) {
    FollowKeyField field;
    field.setState(true, dDorian(), std::nullopt);
    EXPECT_EQ(field.getText(), "Song key" + kDot + "D Dorian");
    EXPECT_EQ(field.getDescription(), "Following song key, D Dorian");
    EXPECT_EQ(field.getTitle(), "Key, following song key");
    EXPECT_TRUE(field.getTooltip().startsWith("Following the song key"));

    field.setState(true, std::nullopt, std::nullopt);
    EXPECT_EQ(field.getText(), "Song key" + kDot + "No key");

    field.setState(false, dDorian(), aPhrygian());
    EXPECT_EQ(field.getText(), "A Phrygian" + kDot + "own");
    EXPECT_EQ(field.getDescription(), "Own key, A Phrygian");
    EXPECT_EQ(field.getTooltip(), "Own key. Choose Follow song key to follow again.");
    EXPECT_EQ(field.getTitle(), "Key, own key");
}

TEST(FollowKeyFieldTest, AFollowerIsMutedAndAnOwnKeyIsNot) {
    FollowKeyField field;
    field.setState(true, dDorian(), std::nullopt);
    const auto muted = field.findColour(juce::ComboBox::textColourId);
    field.setState(false, dDorian(), aPhrygian());
    EXPECT_NE(field.findColour(juce::ComboBox::textColourId), muted);
}

TEST(FollowKeyFieldTest, OnlyAFollowedKeyChangeGlows) {
    MotionOn motion;
    FollowKeyField field;
    field.setState(true, dDorian(), std::nullopt);
    EXPECT_EQ(field.getGlow(), 0.0f) << "the first look never glows";

    field.setState(true, eMinor(), std::nullopt);
    EXPECT_GT(field.getGlow(), 0.0f);
    field.finishGlowForTest();
    EXPECT_EQ(field.getGlow(), 0.0f);

    field.setState(false, eMinor(), aPhrygian());
    EXPECT_EQ(field.getGlow(), 0.0f) << "picking an own key is the user's own change";
    field.setState(false, dDorian(), aPhrygian());
    EXPECT_EQ(field.getGlow(), 0.0f) << "an own key does not move with the song key";
    field.setState(true, dDorian(), aPhrygian());
    EXPECT_EQ(field.getGlow(), 0.0f) << "turning following on is the user's own change";
}

TEST(FollowKeyFieldTest, ReducedMotionChangesTheTextWithoutAGlow) {
    synth::ui::setReducedMotionForTest(true);
    FollowKeyField field;
    field.setState(true, dDorian(), std::nullopt);
    field.setState(true, eMinor(), std::nullopt);
    EXPECT_EQ(field.getGlow(), 0.0f);
    EXPECT_EQ(field.getText(), "Song key" + kDot + "E Natural Minor");
    synth::ui::setReducedMotionForTest(std::nullopt);
}

TEST(FollowKeyFieldTest, ThePickerHasTheFollowRowAndReportsEachChoice) {
    FollowKeyField field;
    std::unique_ptr<juce::Component> held;
    field.pickerLauncher = [&](std::unique_ptr<juce::Component> picker, juce::Rectangle<int>) {
        held = std::move(picker);
    };
    std::optional<std::optional<synth::MusicalScale>> picked;
    int followed = 0;
    field.onPickOwn = [&](std::optional<synth::MusicalScale> key) { picked = key; };
    field.onFollow = [&] { ++followed; };
    field.setState(true, dDorian(), std::nullopt);

    field.openPicker();
    auto* picker = dynamic_cast<KeyPicker*>(held.get());
    ASSERT_NE(picker, nullptr);
    EXPECT_EQ(field.getOpenPicker(), picker);
    field.openPicker();
    EXPECT_EQ(dynamic_cast<KeyPicker*>(held.get()), picker) << "one picker at a time";

    picker->setQuery("a phr");
    picker->pickHighlighted();
    ASSERT_TRUE(picked.has_value());
    ASSERT_TRUE(picked->has_value());
    EXPECT_EQ(synth::keyDisplayName(**picked), "A Phrygian");

    held.reset();
    field.setState(false, dDorian(), aPhrygian());
    field.openPicker();
    picker = dynamic_cast<KeyPicker*>(held.get());
    ASSERT_NE(picker, nullptr);
    picker->clickFollow();
    EXPECT_EQ(followed, 1);
}
