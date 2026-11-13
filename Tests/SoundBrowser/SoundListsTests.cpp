// SoundListsTests.cpp -- favourites and the recent list: ordering, the 50 cap and the settings round trip.
#include "SoundBrowser/SoundLists.h"
#include <gtest/gtest.h>

using synth::soundbrowser::SoundLists;

TEST(SoundListsTest, ToggleFlipsAFavouriteAndReportsTheNewState) {
    SoundLists lists;
    int changes = 0;
    lists.onChanged = [&] { ++changes; };
    EXPECT_TRUE(lists.toggleFavourite("/a.wav"));
    EXPECT_TRUE(lists.isFavourite("/a.wav"));
    EXPECT_FALSE(lists.toggleFavourite("/a.wav"));
    EXPECT_FALSE(lists.isFavourite("/a.wav"));
    EXPECT_EQ(changes, 2);
}

TEST(SoundListsTest, RecentKeepsTheNewestFiftyWithRepeatsMovedToTheFront) {
    SoundLists lists;
    for (int i = 0; i < 60; ++i)
        lists.noteUsed("/s" + juce::String(i) + ".wav");
    EXPECT_EQ(lists.recent().size(), SoundLists::kMaxRecent);
    EXPECT_EQ(lists.recent()[0], "/s59.wav");
    EXPECT_FALSE(lists.recent().contains("/s5.wav"));
    lists.noteUsed("/s30.wav");
    EXPECT_EQ(lists.recent()[0], "/s30.wav");
    EXPECT_EQ(lists.recent().size(), SoundLists::kMaxRecent);
    EXPECT_EQ(lists.recent().indexOf("/s30.wav"), 0);
}

TEST(SoundListsTest, ASettingStringRoundTripsAndAnOversizedSavedListIsTrimmed) {
    juce::StringArray paths{"/one two.wav", "/three.wav"};
    EXPECT_EQ(SoundLists::fromSetting(SoundLists::toSetting(paths)), paths);
    EXPECT_TRUE(SoundLists::fromSetting("").isEmpty());

    juce::StringArray many;
    for (int i = 0; i < 80; ++i)
        many.add("/s" + juce::String(i));
    SoundLists lists;
    lists.setRecent(many);
    EXPECT_EQ(lists.recent().size(), 50);
}

TEST(SoundListsTest, ForgetTakesAnIdOffBothListsAndReplaceKeepsItsPlace) {
    SoundLists lists;
    int changes = 0;
    lists.onChanged = [&] { ++changes; };
    lists.toggleFavourite("a");
    lists.toggleFavourite("b");
    lists.noteUsed("b");
    changes = 0;
    lists.replace("b", "c");
    EXPECT_EQ(lists.favourites()[1], "c");
    EXPECT_EQ(lists.recent()[0], "c");
    lists.forget("c");
    EXPECT_EQ(lists.favourites().size(), 1);
    EXPECT_TRUE(lists.recent().isEmpty());
    EXPECT_EQ(changes, 2);
    lists.forget("zzz");
    lists.replace("zzz", "y");
    EXPECT_EQ(changes, 2) << "nothing held it: nothing changed";
}
